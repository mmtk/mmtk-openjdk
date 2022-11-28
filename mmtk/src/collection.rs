use mmtk::util::alloc::AllocationError;
use mmtk::util::opaque_pointer::*;
use mmtk::vm::{Collection, GCThreadContext, Scanning, VMBinding};
use mmtk::{Mutator, MutatorContext};

use crate::UPCALLS;
use crate::{MutatorClosure, OpenJDK};

pub struct VMCollection {}

extern "C" fn report_mutator_stop<F>(
    mutator: *mut Mutator<OpenJDK>,
    callback_ptr: *mut libc::c_void,
) where
    F: FnMut(&'static mut Mutator<OpenJDK>),
{
    let callback: &mut F = unsafe { &mut *(callback_ptr as *mut F) };
    callback(unsafe { &mut *mutator });
}

fn to_mutator_closure<F>(callback: &mut F) -> MutatorClosure
where
    F: FnMut(&'static mut Mutator<OpenJDK>),
{
    MutatorClosure {
        func: report_mutator_stop::<F>,
        data: callback as *mut F as *mut libc::c_void,
    }
}

const GC_THREAD_KIND_CONTROLLER: libc::c_int = 0;
const GC_THREAD_KIND_WORKER: libc::c_int = 1;

impl Collection<OpenJDK> for VMCollection {
    /// With the presence of the "VM companion thread",
    /// the OpenJDK binding allows any MMTk GC thread to stop/start the world.
    const COORDINATOR_ONLY_STW: bool = false;

    fn stop_all_mutators<F>(tls: VMWorkerThread, mut mutator_visitor: F)
    where
        F: FnMut(&'static mut Mutator<OpenJDK>),
    {
        let scan_mutators_in_safepoint =
            <OpenJDK as VMBinding>::VMScanning::SCAN_MUTATORS_IN_SAFEPOINT;

        unsafe {
            ((*UPCALLS).stop_all_mutators)(
                tls,
                scan_mutators_in_safepoint,
                to_mutator_closure(&mut mutator_visitor),
            );
        }
    }

    fn resume_mutators(tls: VMWorkerThread) {
        unsafe {
            ((*UPCALLS).resume_mutators)(tls);
        }
    }

    fn block_for_gc(_tls: VMMutatorThread) {
        unsafe {
            ((*UPCALLS).block_for_gc)();
        }
    }

    fn spawn_gc_thread(tls: VMThread, ctx: GCThreadContext<OpenJDK>) {
        let (ctx_ptr, kind) = match ctx {
            GCThreadContext::Controller(c) => (
                Box::into_raw(c) as *mut libc::c_void,
                GC_THREAD_KIND_CONTROLLER,
            ),
            GCThreadContext::Worker(w) => {
                (Box::into_raw(w) as *mut libc::c_void, GC_THREAD_KIND_WORKER)
            }
        };
        unsafe {
            ((*UPCALLS).spawn_gc_thread)(tls, kind, ctx_ptr);
        }
    }

    fn prepare_mutator<T: MutatorContext<OpenJDK>>(
        _tls_w: VMWorkerThread,
        _tls_m: VMMutatorThread,
        _m: &T,
    ) {
        // unimplemented!()
    }

    fn out_of_memory(tls: VMThread, err_kind: AllocationError) {
        unsafe {
            ((*UPCALLS).out_of_memory)(tls, err_kind);
        }
    }

    fn schedule_finalization(_tls: VMWorkerThread) {
        unsafe {
            ((*UPCALLS).schedule_finalizer)();
        }
    }
}

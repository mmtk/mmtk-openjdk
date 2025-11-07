use mmtk::util::alloc::AllocationError;
use mmtk::util::opaque_pointer::*;
use mmtk::vm::{Collection, GCThreadContext};
use mmtk::Mutator;

use crate::{singleton, UPCALLS};
use crate::{MutatorClosure, OpenJDK};

pub struct VMCollection {}

const GC_THREAD_KIND_WORKER: libc::c_int = 1;

impl<const COMPRESSED: bool> Collection<OpenJDK<COMPRESSED>> for VMCollection {
    fn stop_all_mutators<F>(tls: VMWorkerThread, mut mutator_visitor: F)
    where
        F: FnMut(&'static mut Mutator<OpenJDK<COMPRESSED>>),
    {
        unsafe {
            ((*UPCALLS).stop_all_mutators)(
                tls,
                MutatorClosure::from_rust_closure::<_, COMPRESSED>(&mut mutator_visitor),
            );
        }
    }

    fn resume_mutators(tls: VMWorkerThread) {
        if *crate::singleton::<COMPRESSED>().get_options().plan
            == mmtk::util::options::PlanSelector::ConcurrentImmix
        {
            // For concurrent Immix, we need to check if SATB is active
            let concurrent_plan = singleton::<COMPRESSED>().get_plan().concurrent().unwrap();
            let concurrent_marking_active = concurrent_plan.concurrent_work_in_progress();

            unsafe {
                crate::CONCURRENT_MARKING_ACTIVE = if concurrent_marking_active { 1 } else { 0 };
            }
            log::debug!("Set CONCURRENT_MARKING_ACTIVE to {concurrent_marking_active}");
        }
        unsafe {
            ((*UPCALLS).resume_mutators)(tls);
        }
    }

    fn block_for_gc(_tls: VMMutatorThread) {
        unsafe {
            ((*UPCALLS).block_for_gc)();
        }
    }

    fn spawn_gc_thread(tls: VMThread, ctx: GCThreadContext<OpenJDK<COMPRESSED>>) {
        let (ctx_ptr, kind) = match ctx {
            GCThreadContext::Worker(w) => {
                (Box::into_raw(w) as *mut libc::c_void, GC_THREAD_KIND_WORKER)
            }
        };
        unsafe {
            ((*UPCALLS).spawn_gc_thread)(tls, kind, ctx_ptr);
        }
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

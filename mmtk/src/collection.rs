
use mmtk::vm::Collection;
use mmtk::util::OpaquePointer;
use mmtk::MutatorContext;
use mmtk::scheduler::GCWorker;

use crate::OpenJDK;
use crate::UPCALLS;

pub struct VMCollection {}

impl Collection<OpenJDK> for VMCollection {
    fn stop_all_mutators(tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).stop_all_mutators)(tls);
        }
    }

    fn resume_mutators(tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).resume_mutators)(tls);
        }
    }

    fn block_for_gc(_tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).block_for_gc)();
        }
    }

    fn spawn_worker_thread(tls: OpaquePointer, ctx: Option<&GCWorker<OpenJDK>>) {
        let ctx_ptr = if let Some(r) = ctx {
            r as *const GCWorker<OpenJDK> as *mut GCWorker<OpenJDK>
        } else {
            std::ptr::null_mut()
        };
        unsafe {
            ((*UPCALLS).spawn_worker_thread)(tls, ctx_ptr as usize as _);
        }
    }

    fn prepare_mutator<T: MutatorContext<OpenJDK>>(_tls: OpaquePointer, _m: &T) {
        // unimplemented!()
    }
}
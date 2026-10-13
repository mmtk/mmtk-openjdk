use mmtk::plan::Pause;
use mmtk::util::alloc::AllocationError;
use mmtk::util::opaque_pointer::*;
use mmtk::util::options::PlanSelector;
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
        assert_no_concurrent_span_stw_pause::<COMPRESSED>();
        unsafe {
            ((*UPCALLS).stop_all_mutators)(
                tls,
                MutatorClosure::from_rust_closure::<_, COMPRESSED>(&mut mutator_visitor),
            );
        }
    }

    fn resume_mutators(tls: VMWorkerThread) {
        // For plans with concurrent marking (e.g. ConcurrentImmix and LXR), the barriers check
        // CONCURRENT_MARKING_ACTIVE to decide whether SATB is active. In particular, loads from weak
        // references and weak roots must keep the referent alive during concurrent marking.
        if let Some(concurrent_plan) = singleton::<COMPRESSED>().get_plan().concurrent() {
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

    fn update_weak_processor(lxr: bool) {
        unsafe {
            ((*UPCALLS).update_weak_processor)(lxr);
        }
    }

    fn vm_release() {
        unsafe {
            ((*UPCALLS).gc_epilogue)();
        }
    }

    fn supports_blocking_mutators() -> bool {
        true
    }

    fn block_mutator_until(_tls: VMMutatorThread, unblocked: &dyn Fn() -> bool) {
        extern "C" fn check(data: *mut libc::c_void) -> bool {
            let unblocked = unsafe { &*(data as *const &dyn Fn() -> bool) };
            unblocked()
        }
        let unblocked: &dyn Fn() -> bool = unblocked;
        unsafe {
            ((*UPCALLS).block_mutator_until)(
                check,
                &unblocked as *const &dyn Fn() -> bool as *mut libc::c_void,
            );
        }
    }

    fn wake_blocked_mutators() {
        unsafe {
            ((*UPCALLS).wake_blocked_mutators)();
        }
    }

    fn supports_concurrent_mutator_flush() -> bool {
        true
    }

    fn flush_mutators_concurrently(_tls: VMWorkerThread) {
        unsafe {
            ((*UPCALLS).flush_mutators_concurrently)();
        }
    }
}

/// ConcurrentSpan uses a stop-the-world `Full` pause only for a user-requested collection on a VM
/// that cannot block mutators, or when an option asks for it.  OpenJDK can block mutators
/// (`supports_blocking_mutators`), so `System.gc()` runs a concurrent cycle, and a `Full` pause
/// here means the plan took a path that OpenJDK does not exercise or test.
fn assert_no_concurrent_span_stw_pause<const COMPRESSED: bool>() {
    let mmtk = singleton::<COMPRESSED>();
    if *mmtk.get_options().plan != PlanSelector::ConcurrentSpan {
        return;
    }
    let pause = mmtk
        .get_plan()
        .concurrent()
        .and_then(|plan| plan.current_pause());
    let options = mmtk.get_options();
    assert!(
        pause != Some(Pause::Full)
            || *options.concurrent_span_disable_concurrent_marking
            || !*options.concurrent_span_concurrent_user_gc,
        "ConcurrentSpan scheduled a stop-the-world Full pause on OpenJDK"
    );
}

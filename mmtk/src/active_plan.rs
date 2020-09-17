
use mmtk::{Plan, SelectedPlan};
use mmtk::vm::ActivePlan;
use mmtk::util::OpaquePointer;
use mmtk::scheduler::GCWorker;
use crate::OpenJDK;
use crate::SINGLETON;
use super::UPCALLS;
use std::sync::Mutex;

pub struct VMActivePlan<> {}

impl ActivePlan<OpenJDK> for VMActivePlan {
    fn global() -> &'static SelectedPlan<OpenJDK> {
        &SINGLETON.plan
    }

    unsafe fn worker(tls: OpaquePointer) -> &'static mut GCWorker<OpenJDK> {
        let c = ((*UPCALLS).active_collector)(tls);
        assert!(!c.is_null());
        &mut *c
    }

    unsafe fn is_mutator(tls: OpaquePointer) -> bool {
        ((*UPCALLS).is_mutator)(tls)
    }

    unsafe fn mutator(tls: OpaquePointer) -> &'static mut <SelectedPlan<OpenJDK> as Plan>::Mutator {
        let m = ((*UPCALLS).get_mmtk_mutator)(tls);
        &mut *m
    }

    fn collector_count() -> usize {
        unimplemented!()
    }

    fn reset_mutator_iterator() {
        unsafe {
            ((*UPCALLS).reset_mutator_iterator)();
        }
    }

    fn get_next_mutator() -> Option<&'static mut <SelectedPlan<OpenJDK> as Plan>::Mutator> {
        let _guard = MUTATOR_ITERATOR_LOCK.lock().unwrap();
        unsafe {
            let m = ((*UPCALLS).get_next_mutator)();
            if m.is_null() {
                None
            } else {
                Some(&mut *m)
            }
        }
    }

    fn number_of_mutators() -> usize {
        unsafe {
            ((*UPCALLS).number_of_mutators)()
        }
    }
}

lazy_static! {
    pub static ref MUTATOR_ITERATOR_LOCK: Mutex<()> = Mutex::new(());
}

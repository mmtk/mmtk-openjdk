use super::UPCALLS;
use crate::OpenJDK;
use crate::SINGLETON;
use mmtk::util::OpaquePointer;
use mmtk::vm::ActivePlan;
use mmtk::Mutator;
use mmtk::Plan;
use std::sync::Mutex;

pub struct VMActivePlan {}

impl ActivePlan<OpenJDK> for VMActivePlan {
    fn global() -> &'static dyn Plan<VM = OpenJDK> {
        SINGLETON.get_plan()
    }

    unsafe fn is_mutator(tls: OpaquePointer) -> bool {
        ((*UPCALLS).is_mutator)(tls)
    }

    unsafe fn mutator(tls: OpaquePointer) -> &'static mut Mutator<OpenJDK> {
        let m = ((*UPCALLS).get_mmtk_mutator)(tls);
        &mut *m
    }

    fn reset_mutator_iterator() {
        unsafe {
            ((*UPCALLS).reset_mutator_iterator)();
        }
    }

    fn get_next_mutator() -> Option<&'static mut Mutator<OpenJDK>> {
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
        unsafe { ((*UPCALLS).number_of_mutators)() }
    }
}

lazy_static! {
    pub static ref MUTATOR_ITERATOR_LOCK: Mutex<()> = Mutex::new(());
}

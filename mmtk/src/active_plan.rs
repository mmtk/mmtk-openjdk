use crate::OpenJDK;
use crate::UPCALLS;
use mmtk::util::opaque_pointer::*;
use mmtk::vm::ActivePlan;
use mmtk::Mutator;
use mmtk::Plan;
use std::sync::Mutex;

pub struct VMActivePlan {}

impl<const COMPRESSED: bool> ActivePlan<OpenJDK<COMPRESSED>> for VMActivePlan {
    fn global() -> &'static dyn Plan<VM = OpenJDK<COMPRESSED>> {
        crate::singleton::<COMPRESSED>().get_plan()
    }

    fn is_mutator(tls: VMThread) -> bool {
        unsafe { ((*UPCALLS).is_mutator)(tls) }
    }

    fn mutator(tls: VMMutatorThread) -> &'static mut Mutator<OpenJDK<COMPRESSED>> {
        unsafe {
            let m = ((*UPCALLS).get_mmtk_mutator)(tls);
            &mut *(m as *mut Mutator<OpenJDK<COMPRESSED>>)
        }
    }

    fn reset_mutator_iterator() {
        unsafe {
            ((*UPCALLS).reset_mutator_iterator)();
        }
    }

    fn get_next_mutator() -> Option<&'static mut Mutator<OpenJDK<COMPRESSED>>> {
        let _guard = MUTATOR_ITERATOR_LOCK.lock().unwrap();
        unsafe {
            let m = ((*UPCALLS).get_next_mutator)();
            if m.is_null() {
                None
            } else {
                Some(&mut *(m as *mut Mutator<OpenJDK<COMPRESSED>>))
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

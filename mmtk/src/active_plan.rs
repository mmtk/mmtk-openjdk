use crate::OpenJDK;
use crate::SINGLETON;
use crate::UPCALLS;
use mmtk::util::opaque_pointer::*;
use mmtk::vm::ActivePlan;
use mmtk::Mutator;
use mmtk::Plan;
use std::sync::{Mutex, MutexGuard};

struct OpenJDKMutatorIterator<'a> {
    _guard: MutexGuard<'a, ()>,
}

impl<'a> OpenJDKMutatorIterator<'a> {
    fn new(guard: MutexGuard<'a, ()>) -> Self {
        // Reset mutator iterator
        unsafe {
            ((*UPCALLS).reset_mutator_iterator)();
        }
        Self { _guard: guard }
    }
}

impl<'a> Iterator for OpenJDKMutatorIterator<'a> {
    type Item = &'a mut Mutator<OpenJDK>;

    fn next(&mut self) -> Option<Self::Item> {
        let next = unsafe { ((*UPCALLS).get_next_mutator)() };
        if next.is_null() {
            None
        } else {
            Some(unsafe { &mut *next })
        }
    }
}

pub struct VMActivePlan {}

impl ActivePlan<OpenJDK> for VMActivePlan {
    fn global() -> &'static dyn Plan<VM = OpenJDK> {
        SINGLETON.get_plan()
    }

    fn is_mutator(tls: VMThread) -> bool {
        unsafe { ((*UPCALLS).is_mutator)(tls) }
    }

    fn mutator(tls: VMMutatorThread) -> &'static mut Mutator<OpenJDK> {
        unsafe {
            let m = ((*UPCALLS).get_mmtk_mutator)(tls);
            &mut *m
        }
    }

    fn mutators<'a>() -> Box<dyn Iterator<Item = &'a mut Mutator<OpenJDK>> + 'a> {
        let guard = MUTATOR_ITERATOR_LOCK.lock().unwrap();
        Box::new(OpenJDKMutatorIterator::new(guard))
    }

    fn number_of_mutators() -> usize {
        unsafe { ((*UPCALLS).number_of_mutators)() }
    }
}

lazy_static! {
    pub static ref MUTATOR_ITERATOR_LOCK: Mutex<()> = Mutex::new(());
}

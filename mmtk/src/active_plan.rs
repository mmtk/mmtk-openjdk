use crate::abi::JavaThreadIteratorWithHandle;
use crate::OpenJDK;
use crate::SINGLETON;
use crate::UPCALLS;
use mmtk::util::opaque_pointer::*;
use mmtk::vm::ActivePlan;
use mmtk::Mutator;
use mmtk::Plan;
use std::marker::PhantomData;
use std::mem::MaybeUninit;

struct OpenJDKMutatorIterator<'a> {
    handle: MaybeUninit<JavaThreadIteratorWithHandle>,
    _p: PhantomData<&'a ()>,
}

impl<'a> OpenJDKMutatorIterator<'a> {
    fn new() -> Self {
        let mut iter = Self {
            handle: MaybeUninit::uninit(),
            _p: PhantomData,
        };
        // Create JavaThreadIteratorWithHandle
        unsafe {
            ((*UPCALLS).new_java_thread_iterator)(iter.handle.as_mut_ptr());
        }
        iter
    }
}

impl<'a> Iterator for OpenJDKMutatorIterator<'a> {
    type Item = &'a mut Mutator<OpenJDK>;

    fn next(&mut self) -> Option<Self::Item> {
        let next = unsafe { ((*UPCALLS).java_thread_iterator_next)(self.handle.as_mut_ptr()) };
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
        Box::new(OpenJDKMutatorIterator::new())
    }

    fn number_of_mutators() -> usize {
        unsafe { ((*UPCALLS).number_of_mutators)() }
    }
}

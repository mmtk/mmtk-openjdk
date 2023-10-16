use crate::MutatorClosure;
use crate::OpenJDK;
use crate::UPCALLS;
use mmtk::util::opaque_pointer::*;
use mmtk::vm::ActivePlan;
use mmtk::Mutator;
use std::collections::VecDeque;
use std::marker::PhantomData;

struct OpenJDKMutatorIterator<'a, const COMPRESSED: bool> {
    mutators: VecDeque<&'a mut Mutator<OpenJDK<COMPRESSED>>>,
    phantom_data: PhantomData<&'a ()>,
}

impl<'a, const COMPRESSED: bool> OpenJDKMutatorIterator<'a, COMPRESSED> {
    fn new() -> Self {
        let mut mutators = VecDeque::new();
        unsafe {
            ((*UPCALLS).get_mutators)(MutatorClosure::from_rust_closure(&mut |mutator| {
                mutators.push_back(mutator);
            }));
        }
        Self {
            mutators,
            phantom_data: PhantomData,
        }
    }
}

impl<'a, const COMPRESSED: bool> Iterator for OpenJDKMutatorIterator<'a, COMPRESSED> {
    type Item = &'a mut Mutator<OpenJDK<COMPRESSED>>;

    fn next(&mut self) -> Option<Self::Item> {
        self.mutators.pop_front()
    }
}

pub struct VMActivePlan {}

impl<const COMPRESSED: bool> ActivePlan<OpenJDK<COMPRESSED>> for VMActivePlan {
    fn is_mutator(tls: VMThread) -> bool {
        unsafe { ((*UPCALLS).is_mutator)(tls) }
    }

    fn mutator(tls: VMMutatorThread) -> &'static mut Mutator<OpenJDK<COMPRESSED>> {
        unsafe {
            let m = ((*UPCALLS).get_mmtk_mutator)(tls);
            &mut *(m as *mut Mutator<OpenJDK<COMPRESSED>>)
        }
    }

    fn mutators<'a>() -> Box<dyn Iterator<Item = &'a mut Mutator<OpenJDK<COMPRESSED>>> + 'a> {
        Box::new(OpenJDKMutatorIterator::<COMPRESSED>::new())
    }

    fn number_of_mutators() -> usize {
        unsafe { ((*UPCALLS).number_of_mutators)() }
    }
}

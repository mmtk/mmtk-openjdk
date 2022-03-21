use crate::abi::{InstanceRefKlass, Oop};
use crate::OpenJDK;
use crate::UPCALLS;
use mmtk::util::ObjectReference;
use mmtk::vm::ReferenceGlue;

pub struct VMReferenceGlue {}

impl ReferenceGlue<OpenJDK> for VMReferenceGlue {
    fn set_referent(reff: ObjectReference, referent: ObjectReference) {
        let oop = Oop::from(reff);
        unsafe { InstanceRefKlass::referent_address(oop).store(referent) };
    }
    fn get_referent(object: ObjectReference) -> ObjectReference {
        let oop = Oop::from(object);
        unsafe { InstanceRefKlass::referent_address(oop).load::<ObjectReference>() }
    }
    fn enqueue_references(references: &[ObjectReference]) {
        unsafe {
            ((*UPCALLS).enqueue_references)(references, references.len());
        }
    }
}

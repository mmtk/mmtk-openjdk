use crate::abi::{InstanceRefKlass, Oop};
use crate::OpenJDK;
use crate::UPCALLS;
use mmtk::util::opaque_pointer::VMWorkerThread;
use mmtk::util::ObjectReference;
use mmtk::vm::edge_shape::Edge;
use mmtk::vm::ReferenceGlue;

pub struct VMReferenceGlue {}

impl<const COMPRESSED: bool> ReferenceGlue<OpenJDK<COMPRESSED>> for VMReferenceGlue {
    type FinalizableType = ObjectReference;

    fn set_referent(reff: ObjectReference, referent: ObjectReference) {
        let oop = Oop::from(reff);
        InstanceRefKlass::referent_address::<COMPRESSED>(oop).store(referent);
    }
    fn get_referent(object: ObjectReference) -> ObjectReference {
        let oop = Oop::from(object);
        InstanceRefKlass::referent_address::<COMPRESSED>(oop).load()
    }
    fn enqueue_references(references: &[ObjectReference], _tls: VMWorkerThread) {
        unsafe {
            ((*UPCALLS).enqueue_references)(references.as_ptr(), references.len());
        }
    }
}

use super::UPCALLS;
use crate::OpenJDK;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::*;
use mmtk::AllocationSemantics;
use mmtk::CopyContext;

pub struct VMObjectModel {}

impl ObjectModel<OpenJDK> for VMObjectModel {
    #[cfg(target_pointer_width = "64")]
    const GC_BYTE_OFFSET: isize = 7;
    #[cfg(target_pointer_width = "32")]
    const GC_BYTE_OFFSET: isize = 0;

    #[inline]
    fn copy(
        from: ObjectReference,
        allocator: AllocationSemantics,
        copy_context: &mut impl CopyContext,
    ) -> ObjectReference {
        let bytes = unsafe { ((*UPCALLS).get_object_size)(from) };
        assert!(
            bytes <= super::api::get_max_non_los_default_alloc_bytes(),
            "trying to copy object of {} bytes, it should not be allocated into copy space",
            bytes
        );
        let dst =
            copy_context.alloc_copy(from, bytes, ::std::mem::size_of::<usize>(), 0, allocator);
        // Copy
        let src = from.to_address();
        for i in 0..bytes {
            unsafe { (dst + i).store((src + i).load::<u8>()) };
        }
        let to_obj = unsafe { dst.to_object_reference() };
        copy_context.post_copy(to_obj, unsafe { Address::zero() }, bytes, allocator);
        to_obj
    }

    fn copy_to(_from: ObjectReference, _to: ObjectReference, _region: Address) -> Address {
        unimplemented!()
    }

    fn get_reference_when_copied_to(_from: ObjectReference, _to: Address) -> ObjectReference {
        unimplemented!()
    }

    fn get_current_size(object: ObjectReference) -> usize {
        unsafe { ((*UPCALLS).get_object_size)(object) }
    }

    fn get_type_descriptor(_reference: ObjectReference) -> &'static [i8] {
        unimplemented!()
    }

    fn object_start_ref(object: ObjectReference) -> Address {
        object.to_address()
    }

    fn ref_to_address(object: ObjectReference) -> Address {
        object.to_address()
    }

    fn dump_object(object: ObjectReference) {
        unsafe {
            ((*UPCALLS).dump_object)(object);
        }
    }
}

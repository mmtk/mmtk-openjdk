
use mmtk::vm::*;
use mmtk::AllocationSemantic;
use mmtk::util::{Address, ObjectReference};
use mmtk::util::OpaquePointer;
use std::sync::atomic::{AtomicU8, AtomicUsize, Ordering};
use super::UPCALLS;
use crate::OpenJDK;
use mmtk::CopyContext;

pub struct VMObjectModel {}

impl ObjectModel<OpenJDK> for VMObjectModel {
    #[cfg(target_pointer_width = "64")]
    const GC_BYTE_OFFSET: usize = 56;
    #[cfg(target_pointer_width = "32")]
    const GC_BYTE_OFFSET: usize = 0;
    fn get_gc_byte(o: ObjectReference) -> &'static AtomicU8 {
        unsafe {
            &*(o.to_address() + Self::GC_BYTE_OFFSET / 8).to_ptr::<AtomicU8>()
        }
    }
    #[inline]
    fn copy(from: ObjectReference, allocator: AllocationSemantic, copy_context: &mut impl CopyContext) -> ObjectReference {
        let bytes = unsafe { ((*UPCALLS).get_object_size)(from) };
        let dst = copy_context.alloc_copy(from, bytes, ::std::mem::size_of::<usize>(), 0, allocator);
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

    fn get_size_when_copied(_object: ObjectReference) -> usize {
        unimplemented!()
    }

    fn get_align_when_copied(_object: ObjectReference) -> usize {
        unimplemented!()
    }

    fn get_align_offset_when_copied(_object: ObjectReference) -> isize {
        unimplemented!()
    }

    fn get_current_size(object: ObjectReference) -> usize {
        unsafe { ((*UPCALLS).get_object_size)(object) }
    }

    fn get_next_object(_object: ObjectReference) -> ObjectReference {
        unimplemented!()
    }

    unsafe fn get_object_from_start_address(_start: Address) -> ObjectReference {
        unimplemented!()
    }

    fn get_object_end_address(_object: ObjectReference) -> Address {
        unimplemented!()
    }

    fn get_type_descriptor(_reference: ObjectReference) -> &'static [i8] {
        unimplemented!()
    }

    fn is_array(_object: ObjectReference) -> bool {
        unimplemented!()
    }

    fn is_primitive_array(_object: ObjectReference) -> bool {
        unimplemented!()
    }

    fn get_array_length(_object: ObjectReference) -> usize {
        unimplemented!()
    }

    fn attempt_available_bits(object: ObjectReference, old: usize, new: usize) -> bool {
        unsafe {
            object.to_address().compare_exchange::<AtomicUsize>(old, new, Ordering::SeqCst, Ordering::SeqCst).is_ok()
        }
    }

    fn prepare_available_bits(object: ObjectReference) -> usize {
        unsafe { object.to_address().load() }
    }

    fn write_available_byte(_object: ObjectReference, _val: u8) {
        unimplemented!()
    }

    fn read_available_byte(_object: ObjectReference) -> u8 {
        unimplemented!()
    }

    fn write_available_bits_word(object: ObjectReference, val: usize) {
        unsafe { object.to_address().atomic_store::<AtomicUsize>(val, Ordering::SeqCst) }
    }

    fn read_available_bits_word(object: ObjectReference) -> usize {
        unsafe { object.to_address().atomic_load::<AtomicUsize>(Ordering::SeqCst) }
    }

    fn gc_header_offset() -> isize {
        0
    }

    fn object_start_ref(object: ObjectReference) -> Address {
        object.to_address()
    }

    fn ref_to_address(object: ObjectReference) -> Address {
        object.to_address()
    }

    fn is_acyclic(_typeref: ObjectReference) -> bool {
        unimplemented!()
    }

    fn dump_object(object: ObjectReference) {
        unsafe {
            ((*UPCALLS).dump_object)(object);
        }
    }

    fn get_array_base_offset() -> isize {
        unimplemented!()
    }

    fn array_base_offset_trapdoor<T>(_o: T) -> isize {
        unimplemented!()
    }

    fn get_array_length_offset() -> isize {
        unimplemented!()
    }
}

use std::sync::atomic::Ordering;

use super::UPCALLS;
use crate::{vm_metadata, OpenJDK};
use mmtk::util::alloc::fill_alignment_gap;
use mmtk::util::metadata::header_metadata::HeaderMetadataSpec;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::*;
use mmtk::AllocationSemantics;
use mmtk::CopyContext;

pub struct VMObjectModel {}

impl ObjectModel<OpenJDK> for VMObjectModel {
    // For now we use the default const from mmtk-core
    const GLOBAL_LOG_BIT_SPEC: VMGlobalLogBitSpec = vm_metadata::LOGGING_SIDE_METADATA_SPEC;

    const LOCAL_FORWARDING_POINTER_SPEC: VMLocalForwardingPointerSpec =
        vm_metadata::FORWARDING_POINTER_METADATA_SPEC;
    const LOCAL_FORWARDING_BITS_SPEC: VMLocalForwardingBitsSpec =
        vm_metadata::FORWARDING_BITS_METADATA_SPEC;
    const LOCAL_MARK_BIT_SPEC: VMLocalMarkBitSpec = vm_metadata::MARKING_METADATA_SPEC;
    const LOCAL_LOS_MARK_NURSERY_SPEC: VMLocalLOSMarkNurserySpec = vm_metadata::LOS_METADATA_SPEC;

    #[inline(always)]
    fn load_metadata(
        metadata_spec: &HeaderMetadataSpec,
        object: ObjectReference,
        mask: Option<usize>,
        atomic_ordering: Option<Ordering>,
    ) -> usize {
        vm_metadata::load_metadata(metadata_spec, object, mask, atomic_ordering)
    }

    #[inline(always)]
    fn store_metadata(
        metadata_spec: &HeaderMetadataSpec,
        object: ObjectReference,
        val: usize,
        mask: Option<usize>,
        atomic_ordering: Option<Ordering>,
    ) {
        vm_metadata::store_metadata(metadata_spec, object, val, mask, atomic_ordering);
    }

    #[inline(always)]
    fn compare_exchange_metadata(
        metadata_spec: &HeaderMetadataSpec,
        object: ObjectReference,
        old_val: usize,
        new_val: usize,
        mask: Option<usize>,
        success_order: Ordering,
        failure_order: Ordering,
    ) -> bool {
        vm_metadata::compare_exchange_metadata(
            metadata_spec,
            object,
            old_val,
            new_val,
            mask,
            success_order,
            failure_order,
        )
    }

    #[inline(always)]
    fn fetch_add_metadata(
        metadata_spec: &HeaderMetadataSpec,
        object: ObjectReference,
        val: usize,
        order: Ordering,
    ) -> usize {
        vm_metadata::fetch_add_metadata(metadata_spec, object, val, order)
    }

    #[inline(always)]
    fn fetch_sub_metadata(
        metadata_spec: &HeaderMetadataSpec,
        object: ObjectReference,
        val: usize,
        order: Ordering,
    ) -> usize {
        vm_metadata::fetch_sub_metadata(metadata_spec, object, val, order)
    }

    #[inline]
    fn copy(
        from: ObjectReference,
        allocator: AllocationSemantics,
        copy_context: &mut impl CopyContext,
    ) -> ObjectReference {
        let bytes = unsafe { ((*UPCALLS).get_object_size)(from) };
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

    fn copy_to(from: ObjectReference, to: ObjectReference, region: Address) -> Address {
        let need_copy = from != to;
        let bytes = unsafe { ((*UPCALLS).get_object_size)(from) };
        if need_copy {
            // copy obj to target
            let dst = to.to_address();
            // Copy
            let src = from.to_address();
            for i in 0..bytes {
                unsafe { (dst + i).store((src + i).load::<u8>()) };
            }
        }
        let start = Self::object_start_ref(to);
        if region != Address::ZERO {
            fill_alignment_gap::<OpenJDK>(region, start);
        }
        start + bytes
    }

    fn get_reference_when_copied_to(_from: ObjectReference, _to: Address) -> ObjectReference {
        unimplemented!()
    }

    fn get_current_size(object: ObjectReference) -> usize {
        unsafe { ((*UPCALLS).get_object_size)(object) }
    }

    fn get_size_when_copied(object: ObjectReference) -> usize {
        Self::get_current_size(object)
    }

    fn get_align_when_copied(_object: ObjectReference) -> usize {
        // FIXME figure out the proper alignment
        ::std::mem::size_of::<usize>()
    }

    fn get_align_offset_when_copied(_object: ObjectReference) -> isize {
        0
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

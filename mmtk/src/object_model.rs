use std::sync::atomic::Ordering;

use super::UPCALLS;
use crate::{vm_metadata, OpenJDK};
use mmtk::util::metadata::{HeaderMetadataSpec, MetadataSpec};
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::*;
use mmtk::AllocationSemantics;
use mmtk::CopyContext;

pub struct VMObjectModel {}

impl ObjectModel<OpenJDK> for VMObjectModel {
    // For now we use the default const from mmtk-core
    const GLOBAL_LOG_BIT_SPEC: MetadataSpec = vm_metadata::LOGGING_SIDE_METADATA_SPEC;

    const LOCAL_FORWARDING_POINTER_SPEC: MetadataSpec =
        vm_metadata::FORWARDING_POINTER_METADATA_SPEC;
    const LOCAL_FORWARDING_BITS_SPEC: MetadataSpec = vm_metadata::FORWARDING_BITS_METADATA_SPEC;
    const LOCAL_MARK_BIT_SPEC: MetadataSpec = vm_metadata::MARKING_METADATA_SPEC;
    const LOCAL_LOS_MARK_NURSERY_SPEC: MetadataSpec = vm_metadata::LOS_METADATA_SPEC;

    #[inline(always)]
    fn load_metadata(
        metadata_spec: HeaderMetadataSpec,
        object: ObjectReference,
        mask: Option<usize>,
        atomic_ordering: Option<Ordering>,
    ) -> usize {
        vm_metadata::load_metadata(metadata_spec, object, mask, atomic_ordering)
    }

    #[inline(always)]
    fn store_metadata(
        metadata_spec: HeaderMetadataSpec,
        object: ObjectReference,
        val: usize,
        mask: Option<usize>,
        atomic_ordering: Option<Ordering>,
    ) {
        vm_metadata::store_metadata(metadata_spec, object, val, mask, atomic_ordering);
    }

    #[inline(always)]
    fn compare_exchange_metadata(
        metadata_spec: HeaderMetadataSpec,
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
        metadata_spec: HeaderMetadataSpec,
        object: ObjectReference,
        val: usize,
        order: Ordering,
    ) -> usize {
        vm_metadata::fetch_add_metadata(metadata_spec, object, val, order)
    }

    #[inline(always)]
    fn fetch_sub_metadata(
        metadata_spec: HeaderMetadataSpec,
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

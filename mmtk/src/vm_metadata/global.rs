use mmtk::util::{metadata as mmtk_meta, ObjectReference};
use std::sync::atomic::Ordering;

/// This function implements the `load_metadata` method from the `ObjectModel` trait.
#[inline(always)]
pub(crate) fn load_metadata(
    metadata_spec: &mmtk_meta::header_metadata::HeaderMetadataSpec,
    object: ObjectReference,
    optional_mask: Option<usize>,
    atomic_ordering: Option<Ordering>,
) -> usize {
    mmtk_meta::header_metadata::load_metadata(metadata_spec, object, optional_mask, atomic_ordering)
}

/// This function implements the `store_metadata` method from the `ObjectModel` trait.
#[inline(always)]
pub(crate) fn store_metadata(
    metadata_spec: &mmtk_meta::header_metadata::HeaderMetadataSpec,
    object: ObjectReference,
    val: usize,
    optional_mask: Option<usize>,
    atomic_ordering: Option<Ordering>,
) {
    mmtk_meta::header_metadata::store_metadata(
        metadata_spec,
        object,
        val,
        optional_mask,
        atomic_ordering,
    )
}

/// This function implements the `compare_exchange_metadata` method from the `ObjectModel` trait.
#[inline(always)]
pub(crate) fn compare_exchange_metadata(
    metadata_spec: &mmtk_meta::header_metadata::HeaderMetadataSpec,
    object: ObjectReference,
    old_metadata: usize,
    new_metadata: usize,
    optional_mask: Option<usize>,
    success_order: Ordering,
    failure_order: Ordering,
) -> bool {
    mmtk_meta::header_metadata::compare_exchange_metadata(
        metadata_spec,
        object,
        old_metadata,
        new_metadata,
        optional_mask,
        success_order,
        failure_order,
    )
}

/// This function implements the `fetch_add_metadata` method from the `ObjectModel` trait.
#[inline(always)]
pub(crate) fn fetch_add_metadata(
    metadata_spec: &mmtk_meta::header_metadata::HeaderMetadataSpec,
    object: ObjectReference,
    val: usize,
    order: Ordering,
) -> usize {
    mmtk_meta::header_metadata::fetch_add_metadata(metadata_spec, object, val, order)
}

/// This function implements the `fetch_sub_metadata` method from the `ObjectModel` trait.
#[inline(always)]
pub(crate) fn fetch_sub_metadata(
    metadata_spec: &mmtk_meta::header_metadata::HeaderMetadataSpec,
    object: ObjectReference,
    val: usize,
    order: Ordering,
) -> usize {
    mmtk_meta::header_metadata::fetch_sub_metadata(metadata_spec, object, val, order)
}

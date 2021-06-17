pub(super) use mmtk::util::constants::{LOG_BITS_IN_BYTE, LOG_BITS_IN_WORD, LOG_MIN_OBJECT_SIZE};
#[cfg(target_pointer_width = "32")]
use mmtk::util::metadata::metadata_bytes_per_chunk;
use mmtk::util::metadata::side_metadata::{
    metadata_address_range_size, GLOBAL_SIDE_METADATA_BASE_ADDRESS,
    LOCAL_SIDE_METADATA_BASE_ADDRESS,
};
use mmtk::util::metadata::MetadataSpec;

/// Return the space size of the given metadata spec.
/// For 32-bits openjdk:
///  - For global metadata, the space size is the whole address range size, and
///  - For local metadata, the space size is the size of metadata space needed per chunk.
///
#[cfg(target_pointer_width = "32")]
const fn side_metadata_size(metadata_spec: &MetadataSpec) -> usize {
    if metadata_spec.is_global {
        metadata_address_range_size(metadata_spec)
    } else {
        metadata_bytes_per_chunk(metadata_spec.log_min_obj_size, metadata_spec.num_of_bits)
    }
}

/// Return the space size of the given metadata spec.
/// For 64-bits openjdk:
///  - For both global and local metadata, the space size is the whole address range size.
///
#[cfg(target_pointer_width = "64")]
const fn side_metadata_size(metadata_spec: &MetadataSpec) -> usize {
    metadata_address_range_size(metadata_spec)
}

pub(crate) const LOG_BITS_IN_U16: usize = 4;
pub(crate) const LOG_BITS_IN_U32: usize = 5;
#[cfg(target_pointer_width = "64")]
pub(crate) const FORWARDING_BITS_OFFSET: usize = 56;
#[cfg(target_pointer_width = "32")]
pub(crate) const FORWARDING_BITS_OFFSET: usize = 0;

pub(crate) const FORWARDING_POINTER_OFFSET: usize = 0;

// Global MetadataSpecs - Start

/// Global logging bit metadata spec
/// 1 bit per object
pub(crate) const LOGGING_SIDE_METADATA_SPEC: MetadataSpec = MetadataSpec {
    is_side_metadata: true,
    is_global: true,
    offset: GLOBAL_SIDE_METADATA_BASE_ADDRESS.as_isize(),
    num_of_bits: 1,
    log_min_obj_size: 3,
};

// Global MetadataSpecs - End

// PolicySpecific MetadataSpecs - Start

/// PolicySpecific forwarding pointer metadata spec
/// 1 word per object
pub(crate) const FORWARDING_POINTER_METADATA_SPEC: MetadataSpec = MetadataSpec {
    is_side_metadata: false,
    is_global: false,
    offset: FORWARDING_POINTER_OFFSET as isize,
    num_of_bits: 1 << LOG_BITS_IN_WORD,
    log_min_obj_size: LOG_MIN_OBJECT_SIZE as usize,
};

/// PolicySpecific object forwarding status metadata spec
/// 2 bits per object
pub(crate) const FORWARDING_BITS_METADATA_SPEC: MetadataSpec = MetadataSpec {
    is_side_metadata: false,
    is_global: false,
    offset: FORWARDING_BITS_OFFSET as isize,
    num_of_bits: 2,
    log_min_obj_size: LOG_MIN_OBJECT_SIZE as usize,
};

/// PolicySpecific mark bit metadata spec
/// 1 bit per object
pub(crate) const MARKING_METADATA_SPEC: MetadataSpec = MetadataSpec {
    is_side_metadata: false,
    is_global: false,
    offset: FORWARDING_BITS_OFFSET as isize,
    num_of_bits: 1,
    log_min_obj_size: LOG_MIN_OBJECT_SIZE as usize,
};

/// PolicySpecific mark-and-nursery bits metadata spec
/// 2-bits per object
pub(crate) const LOS_METADATA_SPEC: MetadataSpec = MetadataSpec {
    is_side_metadata: false,
    is_global: false,
    offset: FORWARDING_BITS_OFFSET as isize,
    num_of_bits: 2,
    log_min_obj_size: LOG_MIN_OBJECT_SIZE as usize,
};

// TODO: This is not used now, but probably needs to be double checked before being used.
#[cfg(target_pointer_width = "64")]
pub(crate) const UNLOGGED_SIDE_METADATA_SPEC: MetadataSpec = MetadataSpec {
    is_side_metadata: true,
    is_global: false,
    offset: LOCAL_SIDE_METADATA_BASE_ADDRESS.as_isize(),
    num_of_bits: 1,
    log_min_obj_size: LOG_MIN_OBJECT_SIZE as usize,
};

#[cfg(target_pointer_width = "32")]
pub(crate) const UNLOGGED_SIDE_METADATA_SPEC: MetadataSpec = MetadataSpec {
    is_side_metadata: true,
    is_global: false,
    offset: 0,
    num_of_bits: 1,
    log_min_obj_size: LOG_MIN_OBJECT_SIZE as usize,
};

// PolicySpecific MetadataSpecs - End

pub(crate) const LAST_GLOBAL_SIDE_METADATA_OFFSET: usize =
    GLOBAL_SIDE_METADATA_BASE_ADDRESS.as_usize() + side_metadata_size(&LOGGING_SIDE_METADATA_SPEC);

pub(crate) const LAST_LOCAL_SIDE_METADATA_OFFSET: usize =
    UNLOGGED_SIDE_METADATA_SPEC.offset as usize + side_metadata_size(&UNLOGGED_SIDE_METADATA_SPEC);

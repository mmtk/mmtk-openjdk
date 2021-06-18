pub(super) use mmtk::util::constants::{LOG_BITS_IN_BYTE, LOG_BITS_IN_WORD, LOG_MIN_OBJECT_SIZE};
#[cfg(target_pointer_width = "32")]
use mmtk::util::metadata::metadata_bytes_per_chunk;
use mmtk::util::metadata::side_metadata::{
    SideMetadataSpec, GLOBAL_SIDE_METADATA_BASE_ADDRESS, LOCAL_SIDE_METADATA_BASE_ADDRESS,
};
use mmtk::util::metadata::{HeaderMetadataSpec, MetadataSpec};

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
pub(crate) const LOGGING_SIDE_METADATA_SPEC: MetadataSpec =
    MetadataSpec::OnSide(SideMetadataSpec {
        is_global: true,
        offset: GLOBAL_SIDE_METADATA_BASE_ADDRESS.as_usize(),
        log_num_of_bits: 0,
        log_min_obj_size: 3,
    });

// Global MetadataSpecs - End

// PolicySpecific MetadataSpecs - Start

/// PolicySpecific forwarding pointer metadata spec
/// 1 word per object
pub(crate) const FORWARDING_POINTER_METADATA_SPEC: MetadataSpec =
    MetadataSpec::InHeader(HeaderMetadataSpec {
        bit_offset: FORWARDING_POINTER_OFFSET as isize,
        num_of_bits: 1 << LOG_BITS_IN_WORD,
    });

/// PolicySpecific object forwarding status metadata spec
/// 2 bits per object
pub(crate) const FORWARDING_BITS_METADATA_SPEC: MetadataSpec =
    MetadataSpec::InHeader(HeaderMetadataSpec {
        bit_offset: FORWARDING_BITS_OFFSET as isize,
        num_of_bits: 2,
    });

/// PolicySpecific mark bit metadata spec
/// 1 bit per object
pub(crate) const MARKING_METADATA_SPEC: MetadataSpec = MetadataSpec::InHeader(HeaderMetadataSpec {
    bit_offset: FORWARDING_BITS_OFFSET as isize,
    num_of_bits: 1,
});

/// PolicySpecific mark-and-nursery bits metadata spec
/// 2-bits per object
pub(crate) const LOS_METADATA_SPEC: MetadataSpec = MetadataSpec::InHeader(HeaderMetadataSpec {
    bit_offset: FORWARDING_BITS_OFFSET as isize,
    num_of_bits: 2,
});

// TODO: This is not used now, but probably needs to be double checked before being used.
#[cfg(target_pointer_width = "64")]
pub(crate) const UNLOGGED_SIDE_METADATA_SPEC: MetadataSpec =
    MetadataSpec::OnSide(SideMetadataSpec {
        is_global: false,
        offset: LOCAL_SIDE_METADATA_BASE_ADDRESS.as_usize(),
        log_num_of_bits: 0,
        log_min_obj_size: LOG_MIN_OBJECT_SIZE as usize,
    });

#[cfg(target_pointer_width = "32")]
pub(crate) const UNLOGGED_SIDE_METADATA_SPEC: MetadataSpec =
    MetadataSpec::OnSide(SideMetadataSpec {
        is_global: false,
        offset: 0,
        log_num_of_bits: 0,
        log_min_obj_size: LOG_MIN_OBJECT_SIZE as usize,
    });

// PolicySpecific MetadataSpecs - End

pub(super) use mmtk::util::constants::LOG_BITS_IN_WORD;
use mmtk::util::constants::LOG_BYTES_IN_PAGE;
use mmtk::util::metadata::header_metadata::HeaderMetadataSpec;
use mmtk::util::metadata::side_metadata::{
    SideMetadataSpec, GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS, LOCAL_SIDE_METADATA_VM_BASE_ADDRESS,
};
use mmtk::util::metadata::MetadataSpec;

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
        offset: GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS.as_usize(),
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
pub(crate) const LOS_METADATA_SPEC: MetadataSpec = MetadataSpec::OnSide(SideMetadataSpec {
    is_global: false,
    offset: if cfg!(target_pointer_width = "64") {
        LOCAL_SIDE_METADATA_VM_BASE_ADDRESS.as_usize()
    } else {
        0
    },
    log_num_of_bits: 1,
    log_min_obj_size: LOG_BYTES_IN_PAGE as usize,
});

// PolicySpecific MetadataSpecs - End

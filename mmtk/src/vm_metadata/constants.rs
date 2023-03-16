use mmtk::vm::*;

#[cfg(target_pointer_width = "64")]
pub(crate) const FORWARDING_BITS_OFFSET: isize = 56;
#[cfg(target_pointer_width = "32")]
pub(crate) const FORWARDING_BITS_OFFSET: isize = unimplemented!();

pub(crate) const FORWARDING_POINTER_OFFSET: isize = 0;

// Global MetadataSpecs - Start

/// Global logging bit metadata spec
/// 1 bit per object
pub(crate) const LOGGING_SIDE_METADATA_SPEC: VMGlobalLogBitSpec = VMGlobalLogBitSpec::side_first();

// Global MetadataSpecs - End

// PolicySpecific MetadataSpecs - Start

/// PolicySpecific forwarding pointer metadata spec
/// 1 word per object
pub(crate) const FORWARDING_POINTER_METADATA_SPEC: VMLocalForwardingPointerSpec =
    VMLocalForwardingPointerSpec::in_header(FORWARDING_POINTER_OFFSET);

/// PolicySpecific object forwarding status metadata spec
/// 2 bits per object
pub(crate) const FORWARDING_BITS_METADATA_SPEC: VMLocalForwardingBitsSpec =
    VMLocalForwardingBitsSpec::in_header(FORWARDING_BITS_OFFSET);

/// PolicySpecific mark bit metadata spec
/// 1 bit per object
#[cfg(feature = "mark_bit_in_header")]
pub(crate) const MARKING_METADATA_SPEC: VMLocalMarkBitSpec =
    VMLocalMarkBitSpec::in_header(FORWARDING_BITS_OFFSET);

#[cfg(not(feature = "mark_bit_in_header"))]
pub(crate) const MARKING_METADATA_SPEC: VMLocalMarkBitSpec =
    VMLocalMarkBitSpec::side_after(LOS_METADATA_SPEC.as_spec());

/// PolicySpecific mark-and-nursery bits metadata spec
/// 2-bits per object
pub(crate) const LOS_METADATA_SPEC: VMLocalLOSMarkNurserySpec =
    VMLocalLOSMarkNurserySpec::side_first();

// PolicySpecific MetadataSpecs - End

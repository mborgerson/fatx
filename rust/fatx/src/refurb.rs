use zerocopy::byteorder::little_endian::{U16, U32, U64};
use zerocopy::*;

pub const FATX_REFURB_SIGNATURE: u32 = 0x42524652; // 'RFRB'
pub const FATX_REFURB_OFFSET: usize = 0x600;

// The refurb info, as it appears on disk.
#[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned, Debug)]
#[repr(C, packed)]
pub struct RefurbInfo {
    signature: U32,
    number_of_boots: U32,
    first_power_on: U64,
}

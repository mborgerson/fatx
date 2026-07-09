use zerocopy::byteorder::little_endian::{U16, U32};
use zerocopy::*;

use crate::error::Error;

#[derive(Debug)]
enum FatType {
    Type16,
    Type32,
}

#[derive(Debug)]
pub(crate) enum FatEntry {
    Available,
    Reserved,
    Bad,
    Media,
    End,
    Data(u32),
    Invalid,
}

pub(crate) type FatEntryId = u32;
pub(crate) type ClusterId = u32;

impl From<u16> for FatEntry {
    fn from(value: u16) -> Self {
        match value {
            0x0000 => FatEntry::Available,
            0x0001..0xfff0 => FatEntry::Data(value as u32),
            0xfff0 => FatEntry::Reserved,
            0xfff7 => FatEntry::Bad,
            0xfff8 => FatEntry::Media,
            0xffff => FatEntry::End,
            _ => FatEntry::Invalid,
        }
    }
}

impl From<u32> for FatEntry {
    fn from(value: u32) -> Self {
        match value {
            0x00000000 => FatEntry::Available,
            0x00000001..0xfffffff0 => FatEntry::Data(value),
            0xfffffff0 => FatEntry::Reserved,
            0xfffffff7 => FatEntry::Bad,
            0xfffffff8 => FatEntry::Media,
            0xffffffff => FatEntry::End,
            _ => FatEntry::Invalid,
        }
    }
}

#[derive(Debug)]
pub(crate) struct Fat {
    fat_type: FatType,
    pub(crate) fat_size_bytes: u64,
    pub(crate) fat_data: Vec<u8>, // FIXME: Smarter cache
}

impl Fat {
    /// FAT entry width in bytes (2 for FAT16, 4 for FAT32).
    pub(crate) fn entry_width(&self) -> u8 {
        match self.fat_type {
            FatType::Type16 => 2,
            FatType::Type32 => 4,
        }
    }

    /// End-of-chain marker value for this FAT width.
    pub(crate) fn end_marker(&self) -> u32 {
        match self.fat_type {
            FatType::Type16 => 0xFFFF,
            FatType::Type32 => 0xFFFF_FFFF,
        }
    }

    /// Sets a raw entry value in the in-memory FAT (caller persists to disk).
    pub(crate) fn set_raw(&mut self, index: FatEntryId, value: u32) -> Result<(), Error> {
        match self.fat_type {
            FatType::Type16 => {
                let off = index as usize * 2;
                if off + 2 > self.fat_data.len() {
                    return Err(Error::InvalidClusterNumber);
                }
                self.fat_data[off..off + 2].copy_from_slice(&(value as u16).to_le_bytes());
            }
            FatType::Type32 => {
                let off = index as usize * 4;
                if off + 4 > self.fat_data.len() {
                    return Err(Error::InvalidClusterNumber);
                }
                self.fat_data[off..off + 4].copy_from_slice(&value.to_le_bytes());
            }
        }
        Ok(())
    }

    pub(crate) fn new(num_fat_entries: u32) -> Self {
        // NOTE: this *MUST* be kept below the Cluster Reserved marker for FAT16
        let (fat_type, fat_size_bytes) = if num_fat_entries < 0xfff0 {
            (
                FatType::Type16,
                (num_fat_entries as u64 * 2).next_multiple_of(4096),
            )
        } else {
            (
                FatType::Type32,
                (num_fat_entries as u64 * 4).next_multiple_of(4096),
            )
        };

        let fat_data = vec![0u8; fat_size_bytes as usize];

        Self {
            fat_type,
            fat_size_bytes,
            fat_data,
        }
    }

    pub(crate) fn entry(&mut self, index: FatEntryId) -> Result<FatEntry, Error> {
        match self.fat_type {
            FatType::Type16 => {
                let fat = <[U16]>::ref_from_bytes_with_elems(
                    &self.fat_data[..],
                    (self.fat_size_bytes / 2) as usize,
                )
                .unwrap();
                let value: u16 = fat[index as usize].into();
                Ok(value.into())
            }
            FatType::Type32 => {
                let fat = <[U32]>::ref_from_bytes_with_elems(
                    &self.fat_data[..],
                    (self.fat_size_bytes / 4) as usize,
                )
                .unwrap();
                let value: u32 = fat[index as usize].into();
                Ok(value.into())
            }
        }
    }
}

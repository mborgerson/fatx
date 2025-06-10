use std::io::{Read, Seek, SeekFrom};
use std::path::Path;
use std::sync::{Arc, Mutex, Weak};

use crate::dir::{DirectoryEntry, DirectoryEntryIntoIterator};
use crate::error::Error;
use crate::fat::{ClusterId, Fat};
use crate::file::File;
use crate::partition::DEFAULT_PARTITION_LAYOUT;
use crate::PartitionMapEntry;

use zerocopy::byteorder::little_endian::{U16, U32};
use zerocopy::*;

const FATX_SIGNATURE: u32 = 0x58544146; // 'FATX'
const FATX_FAT_OFFSET_BYTES: u64 = 4096;
const FATX_FAT_RESERVED_ENTRIES_COUNT: u32 = 1;

// The superblock, as it appears on disk.
#[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned, Debug)]
#[repr(C, packed)]
pub(crate) struct Superblock {
    signature: U32,
    volume_id: U32,
    num_sectors_per_cluster: U32,
    root_cluster: U32,
    unknown_0: U16,
    padding: [u8; 4078],
}

#[derive(Debug)]
pub struct FatxFs {
    self_handle: Weak<Mutex<FatxFs>>,

    pub(crate) device_handle: std::fs::File,
    pub(crate) partition_offset_bytes: u64,
    pub(crate) partition_size_bytes: u64,
    pub(crate) num_clusters: u32,
    pub(crate) num_bytes_per_cluster: u64,
    pub(crate) num_entries_per_cluster: u64,
    pub(crate) root_cluster: u32,
    pub(crate) cluster_offset_bytes: u64,
    pub(crate) fat: Fat,
}

pub struct FatxFsConfig {
    device_path: String,
    partition_offset_bytes: u64,
    partition_size_bytes: u64,
    num_bytes_per_sector: u64,
}

impl FatxFsConfig {
    pub fn new(device_path: String) -> Self {
        let layout = &DEFAULT_PARTITION_LAYOUT[3];
        let partition_offset_bytes: u64 = layout.offset_bytes;
        let partition_size_bytes: u64 = layout.size_bytes;

        Self {
            device_path,
            partition_offset_bytes,
            partition_size_bytes,
            num_bytes_per_sector: 512
        }
    }

    pub fn drive_letter(mut self, letter: &str) -> Self {
        let partition_info = PartitionMapEntry::from_letter(letter).expect("invalid partition letter");
        self.partition_offset_bytes = partition_info.offset_bytes;
        self.partition_size_bytes = partition_info.size_bytes;
        self
    }
}

impl FatxFs {
    pub fn open_device(config: &FatxFsConfig) -> Result<FatxFsHandle, Error> {
        // Partition offset and size validation
        if config.partition_offset_bytes % config.num_bytes_per_sector != 0 {
            return Err(Error::InvalidPartitionOffset);
        }
        if config.partition_size_bytes % config.num_bytes_per_sector != 0 {
            return Err(Error::InvalidPartitionSize);
        }

        // Open device
        let mut device_handle = std::fs::File::open(&config.device_path).unwrap();
        device_handle.seek(SeekFrom::Start(config.partition_offset_bytes))?;

        // Read superblock
        let superblock = Superblock::read_from_io(&mut device_handle).unwrap();
        if superblock.signature != FATX_SIGNATURE {
            return Err(Error::InvalidFilesystemSignature);
        }

        // Cluster geometry
        let num_sectors_per_cluster: u64 = superblock.num_sectors_per_cluster.into();
        if !(num_sectors_per_cluster.is_power_of_two() && num_sectors_per_cluster <= 1024) {
            return Err(Error::InvalidSectorsPerCluster);
        }
        let num_bytes_per_cluster = num_sectors_per_cluster * config.num_bytes_per_sector;
        let num_entries_per_cluster: u64 =
            num_bytes_per_cluster / (std::mem::size_of::<DirectoryEntry>() as u64);
        let root_cluster: u32 = superblock.root_cluster.into();

        // Calculate FAT size
        let fat_offset_bytes = config.partition_offset_bytes + FATX_FAT_OFFSET_BYTES;
        let num_fat_entries = (config.partition_size_bytes / num_bytes_per_cluster) as u32;
        if root_cluster >= num_fat_entries {
            log::error!("Root cluster of {} exceeds cluster limit", root_cluster);
            return Err(Error::InvalidRootCluster);
        }

        // FIXME: Make FAT management smarter
        let mut fat = Fat::new(num_fat_entries);
        device_handle.seek(SeekFrom::Start(fat_offset_bytes))?;
        device_handle.read_exact(&mut fat.fat_data)?;

        // Cluster geometry cont'd
        let cluster_offset_bytes = fat_offset_bytes + fat.fat_size_bytes;
        let num_clusters = ((config.partition_size_bytes - fat.fat_size_bytes - FATX_FAT_OFFSET_BYTES)
            / num_bytes_per_cluster
            + FATX_FAT_RESERVED_ENTRIES_COUNT as u64) as u32;

        let fs = Arc::new_cyclic(move |weak_self| {
            Mutex::new(FatxFs {
                self_handle: weak_self.clone(),
                device_handle,
                partition_offset_bytes: config.partition_offset_bytes,
                partition_size_bytes: config.partition_size_bytes,
                num_clusters,
                num_bytes_per_cluster,
                num_entries_per_cluster,
                root_cluster,
                cluster_offset_bytes,
                fat,
            })
        });

        Ok(FatxFsHandle { fs })
    }

    fn handle(&self) -> FatxFsHandle {
        FatxFsHandle {
            fs: self.self_handle.upgrade().unwrap().clone(),
        }
    }

    pub fn cluster_to_byte_offset(&self, cluster: ClusterId) -> Result<u64, Error> {
        if cluster >= self.num_clusters + FATX_FAT_RESERVED_ENTRIES_COUNT {
            return Err(Error::InvalidClusterNumber);
        }

        let byte_offset: u64 = self.cluster_offset_bytes
            + (cluster - FATX_FAT_RESERVED_ENTRIES_COUNT) as u64 * self.num_bytes_per_cluster;
        debug_assert!(byte_offset < (self.partition_offset_bytes + self.partition_size_bytes));

        Ok(byte_offset)
    }

    pub fn seek_cluster(
        &mut self,
        cluster: ClusterId,
        offset_in_cluster: u64,
    ) -> Result<(), Error> {
        self.device_handle.seek(SeekFrom::Start(
            self.cluster_to_byte_offset(cluster).unwrap() + offset_in_cluster,
        ))?;
        Ok(())
    }

    pub fn stat<P: AsRef<Path>>(&mut self, path: P) -> Result<DirectoryEntry, Error> {
        DirectoryEntry::from_path(self, path)
    }

    pub fn open<P: AsRef<Path>>(&mut self, path: P) -> Result<File, Error> {
        let dirent = self.stat(path)?;
        if dirent.is_file() {
            Ok(File::new(self.handle(), dirent))
        } else {
            Err(Error::IsADirectory)
        }
    }

    pub fn read_dir(&mut self, path: &str) -> Result<DirectoryEntryIntoIterator, Error> {
        let dirent = DirectoryEntry::from_path(self, path)?;
        if !dirent.is_directory() {
            return Err(Error::NotADirectory);
        }
        Ok(DirectoryEntryIntoIterator::new(self.handle(), dirent.first_cluster()))
    }
}

pub struct FatxFsHandle {
    pub fs: Arc<Mutex<FatxFs>>, // FIXME: Make private
}

impl FatxFsHandle {
    pub fn with_lock<F, T>(&self, f: F) -> T
    where
        F: FnOnce(&mut FatxFs) -> T,
    {
        let mut fs = self.fs.lock().unwrap();
        f(&mut fs)
    }

    pub fn open(&mut self, path: &str) -> Result<File, Error> {
        self.with_lock(|fs| fs.open(path))
    }

    pub fn stat(&mut self, path: &str) -> Result<DirectoryEntry, Error> {
        self.with_lock(|fs| fs.stat(path))
    }

    pub fn read_dir(&mut self, path: &str) -> Result<DirectoryEntryIntoIterator, Error> {
        self.with_lock(|fs| fs.read_dir(path))
    }
}

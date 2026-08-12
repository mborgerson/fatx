use std::io::{Read, Seek, SeekFrom};
use std::path::Path;
use std::sync::{Arc, Mutex, Weak};

use crate::dir::{DirectoryEntry, DirectoryEntryIntoIterator};
use crate::error::Error;
use crate::fat::{ClusterId, Fat};
use crate::file::File;
use crate::partition::{DEFAULT_PARTITION_LAYOUT, PartitionMapEntry};
use crate::variant::Variant;

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

impl Superblock {
    /// Convert from on-disk form into canonical little-endian.
    ///
    /// Does nothing for the original Xbox; swaps every field for the 360. The
    /// signature is deliberately left alone: it is byte-identical in both
    /// flavours and has already been used to work out which one this is.
    fn normalize(&mut self, variant: Variant) {
        if !variant.needs_swap() {
            return;
        }

        self.volume_id = u32::from(self.volume_id).swap_bytes().into();
        self.num_sectors_per_cluster = u32::from(self.num_sectors_per_cluster).swap_bytes().into();
        self.root_cluster = u32::from(self.root_cluster).swap_bytes().into();
        self.unknown_0 = u16::from(self.unknown_0).swap_bytes().into();
    }
}

#[derive(Debug)]
pub struct FatxFs {
    self_handle: Weak<Mutex<FatxFs>>,

    pub(crate) device_handle: std::fs::File,
    pub(crate) variant: Variant,
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
    variant: Variant,
}

impl FatxFsConfig {
    pub fn new(device_path: String) -> Self {
        let partition = &DEFAULT_PARTITION_LAYOUT[3];
        Self {
            device_path,
            partition_offset_bytes: partition.offset_bytes,
            partition_size_bytes: partition.size_bytes,
            num_bytes_per_sector: 512,
            variant: Variant::Auto,
        }
    }

    pub fn drive_letter(mut self, letter: &str) -> Self {
        let partition_info =
            PartitionMapEntry::from_letter(letter).expect("invalid partition letter");
        self.partition_offset_bytes = partition_info.offset_bytes;
        self.partition_size_bytes = partition_info.size_bytes;
        self
    }

    /// Select an Xbox 360 partition by name.
    pub fn x360_partition(mut self, name: &str) -> Self {
        let partition_info =
            PartitionMapEntry::from_x360_name(name).expect("invalid partition name");
        self.partition_offset_bytes = partition_info.offset_bytes;
        self.partition_size_bytes = partition_info.size_bytes;
        self
    }

    /// Force the on-disk byte order rather than detecting it.
    pub fn variant(mut self, variant: Variant) -> Self {
        self.variant = variant;
        self
    }

    pub fn partition_offset_bytes(mut self, offset: u64) -> Self {
        self.partition_offset_bytes = offset;
        self
    }

    pub fn partition_size_bytes(mut self, size: u64) -> Self {
        self.partition_size_bytes = size;
        self
    }
}

impl FatxFs {
    pub fn open_device(config: &FatxFsConfig) -> Result<FatxFsHandle, Error> {
        // Partition offset and size validation
        if !config
            .partition_offset_bytes
            .is_multiple_of(config.num_bytes_per_sector)
        {
            return Err(Error::InvalidPartitionOffset);
        }
        // Open device
        let mut device_handle = std::fs::File::open(&config.device_path)?;

        // A size of u64::MAX means "the rest of the device", which is how the
        // partitions that run to the end of the disk are described. Resolve it
        // against the device's actual length, rounded down to a whole sector.
        let partition_size_bytes = if config.partition_size_bytes == u64::MAX {
            let device_size = device_handle.seek(SeekFrom::End(0))?;
            if device_size <= config.partition_offset_bytes {
                return Err(Error::InvalidPartitionSize);
            }
            let remaining = device_size - config.partition_offset_bytes;
            remaining - (remaining % config.num_bytes_per_sector)
        } else {
            config.partition_size_bytes
        };

        if !partition_size_bytes.is_multiple_of(config.num_bytes_per_sector) {
            return Err(Error::InvalidPartitionSize);
        }

        device_handle.seek(SeekFrom::Start(config.partition_offset_bytes))?;

        // Read superblock. The raw signature identifies the on-disk byte order,
        // so checking it and resolving the variant are the same step.
        let mut superblock = Superblock::read_from_io(&mut device_handle)?;
        let detected = Variant::from_raw_signature(superblock.signature.into(), FATX_SIGNATURE)
            .ok_or(Error::InvalidFilesystemSignature)?;
        let variant = match config.variant {
            Variant::Auto => {
                log::info!("Detected {detected:?} filesystem");
                detected
            }
            requested if requested == detected => requested,
            requested => {
                log::error!("Filesystem is {detected:?}, but {requested:?} was requested");
                return Err(Error::InvalidFilesystemSignature);
            }
        };
        superblock.normalize(variant);

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
        let num_fat_entries = (partition_size_bytes / num_bytes_per_cluster) as u32;
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
        let num_clusters = ((partition_size_bytes - fat.fat_size_bytes - FATX_FAT_OFFSET_BYTES)
            / num_bytes_per_cluster
            + FATX_FAT_RESERVED_ENTRIES_COUNT as u64) as u32;

        let fs = Arc::new_cyclic(move |weak_self| {
            Mutex::new(FatxFs {
                self_handle: weak_self.clone(),
                device_handle,
                variant,
                partition_offset_bytes: config.partition_offset_bytes,
                partition_size_bytes: partition_size_bytes,
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

    pub(crate) fn cluster_to_byte_offset(&self, cluster: ClusterId) -> Result<u64, Error> {
        if cluster >= self.num_clusters + FATX_FAT_RESERVED_ENTRIES_COUNT {
            return Err(Error::InvalidClusterNumber);
        }

        let byte_offset: u64 = self.cluster_offset_bytes
            + (cluster - FATX_FAT_RESERVED_ENTRIES_COUNT) as u64 * self.num_bytes_per_cluster;
        debug_assert!(byte_offset < (self.partition_offset_bytes + self.partition_size_bytes));

        Ok(byte_offset)
    }

    pub(crate) fn seek_cluster(
        &mut self,
        cluster: ClusterId,
        offset_in_cluster: u64,
    ) -> Result<(), Error> {
        self.device_handle.seek(SeekFrom::Start(
            self.cluster_to_byte_offset(cluster)? + offset_in_cluster,
        ))?;
        Ok(())
    }

    pub(crate) fn stat<P: AsRef<Path>>(&mut self, path: P) -> Result<DirectoryEntry, Error> {
        DirectoryEntry::from_path(self, path)
    }

    pub(crate) fn open<P: AsRef<Path>>(&mut self, path: P) -> Result<File, Error> {
        let dirent = self.stat(path)?;
        if dirent.is_file() {
            Ok(File::new(self.handle(), dirent))
        } else {
            Err(Error::IsADirectory)
        }
    }

    pub(crate) fn read_dir(&mut self, path: &str) -> Result<DirectoryEntryIntoIterator, Error> {
        let dirent = DirectoryEntry::from_path(self, path)?;
        if !dirent.is_directory() {
            return Err(Error::NotADirectory);
        }
        Ok(DirectoryEntryIntoIterator::new(
            self.handle(),
            dirent.first_cluster(),
        ))
    }
}

pub struct FatxFsHandle {
    pub(crate) fs: Arc<Mutex<FatxFs>>, // FIXME: Make private
}

impl FatxFsHandle {
    pub(crate) fn with_lock<F, T>(&self, f: F) -> T
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

    /// The variant this filesystem was opened as, with Auto already resolved
    /// to whichever the signature turned out to be.
    pub fn variant(&self) -> Variant {
        self.with_lock(|fs| fs.variant)
    }
}

use std::path::{Component, Path};

use zerocopy::byteorder::little_endian::{U16, U32};
use zerocopy::*;

const FATX_MAX_FILENAME_LEN: usize = 42;

// Markers used in the filename_size field of the directory entry.
const FATX_DELETED_FILE_MARKER: u8 = 0xe5;
const FATX_END_OF_DIR_MARKER: u8 = 0xff;
const FATX_END_OF_DIR_MARKER2: u8 = 0x00;

// Mask to be applied when reading directory entry attributes.
const FATX_ATTR_READ_ONLY: u8 = 1 << 0;
const FATX_ATTR_SYSTEM: u8 = 1 << 1;
const FATX_ATTR_HIDDEN: u8 = 1 << 2;
const FATX_ATTR_VOLUME: u8 = 1 << 3;
const FATX_ATTR_DIRECTORY: u8 = 1 << 4;

use crate::datetime::DateTime;
use crate::error::Error;
use crate::fat::{ClusterId, FatEntry};
use crate::fs::{FatxFs, FatxFsHandle};
use crate::path::normalize_virtual_path;

// The directory entry, as it appears on disk.
#[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned, Debug)]
#[repr(C, packed)]
pub struct DirectoryEntry {
    filename_len: u8,
    attributes: u8,
    filename_bytes: [u8; FATX_MAX_FILENAME_LEN],
    first_cluster: U32,
    file_size: U32,
    modified_time: U16,
    modified_date: U16,
    created_time: U16,
    created_date: U16,
    accessed_time: U16,
    accessed_date: U16,
}

/// On-disk location of a directory slot (cluster + entry index within it).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DirectoryEntryLocation {
    pub cluster: ClusterId,
    pub index: u64,
}

#[derive(Debug, PartialEq)]
pub enum DirectoryEntryKind {
    Valid,
    Deleted,
    EndOfDirectory,
}

impl DirectoryEntry {
    pub(crate) fn from_path<P: AsRef<Path>>(fs: &mut FatxFs, path: P) -> Result<Self, Error> {
        let path = normalize_virtual_path(path);
        let num_components = path.components().count();

        if num_components == 0 {
            return Err(Error::NotADirectory);
        }

        // Step through path components to find target directory
        let mut cwd = None;
        for (comp_idx, comp) in path.components().enumerate() {
            cwd = match comp {
                Component::RootDir => Some(fs.root_cluster),
                Component::Normal(name) => {
                    if cwd.is_none() {
                        return Err(Error::NotFound);
                    }

                    // Scan directory entries to resolve this path component
                    let mut dir_iter = DirectoryEntryIterator::new(cwd.unwrap());
                    loop {
                        // Fetch next directory entry
                        let dirent_result = dir_iter.next(fs);
                        if dirent_result.is_none() {
                            return Err(Error::NotFound);
                        }

                        // Check current directory entry for target match
                        let dirent = dirent_result.unwrap()?;
                        if let DirectoryEntryKind::Valid = dirent.kind() {
                            if dirent.file_name() != name.to_str().unwrap() {
                                continue;
                            }
                            if comp_idx == (num_components - 1) {
                                return Ok(dirent);
                            }
                            if dirent.is_directory() {
                                break Some(dirent.first_cluster.into());
                            }
                            return Err(Error::NotADirectory);
                        }
                    }
                }
                _ => {
                    panic!("Unexpected path component!");
                }
            }
        }

        // Create a fake DirectoryEntry to represent the root directory
        Ok(Self {
            filename_len: 8,
            attributes: FATX_ATTR_DIRECTORY,
            filename_bytes: {
                let mut filename_bytes = [0u8; FATX_MAX_FILENAME_LEN];
                filename_bytes[0..4].copy_from_slice(b"root");
                filename_bytes
            },
            first_cluster: fs.root_cluster.into(),
            file_size: 0.into(),
            modified_date: 0.into(),
            modified_time: 0.into(),
            created_time: 0.into(),
            created_date: 0.into(),
            accessed_time: 0.into(),
            accessed_date: 0.into(),
        })
    }

    pub(crate) fn kind(&self) -> DirectoryEntryKind {
        match self.filename_len {
            FATX_END_OF_DIR_MARKER => DirectoryEntryKind::EndOfDirectory,
            FATX_END_OF_DIR_MARKER2 => DirectoryEntryKind::EndOfDirectory,
            FATX_DELETED_FILE_MARKER => DirectoryEntryKind::Deleted,
            _ => DirectoryEntryKind::Valid,
        }
    }

    pub fn file_name(&self) -> String {
        assert_eq!(self.kind(), DirectoryEntryKind::Valid);
        let bytes = &self.filename_bytes[..self.filename_len as usize];
        String::from_utf8(bytes.to_vec()).unwrap()
    }

    pub fn file_size(&self) -> u32 {
        self.file_size.into()
    }

    pub fn is_directory(&self) -> bool {
        !self.is_volume() && self.attributes & FATX_ATTR_DIRECTORY == FATX_ATTR_DIRECTORY
    }

    pub fn is_file(&self) -> bool {
        !self.is_volume() && !self.is_directory()
    }

    pub fn is_hidden(&self) -> bool {
        self.attributes & FATX_ATTR_HIDDEN == FATX_ATTR_HIDDEN
    }

    pub fn is_read_only(&self) -> bool {
        self.attributes & FATX_ATTR_READ_ONLY == FATX_ATTR_READ_ONLY
    }

    pub fn is_volume(&self) -> bool {
        self.attributes & FATX_ATTR_VOLUME == FATX_ATTR_VOLUME
    }

    pub fn is_system(&self) -> bool {
        self.attributes & FATX_ATTR_SYSTEM == FATX_ATTR_SYSTEM
    }

    pub fn created(&self) -> DateTime {
        DateTime::from_fatx_encoding(self.created_date.into(), self.created_time.into())
    }

    pub fn modified(&self) -> DateTime {
        DateTime::from_fatx_encoding(self.modified_date.into(), self.modified_time.into())
    }

    pub fn accessed(&self) -> DateTime {
        DateTime::from_fatx_encoding(self.accessed_date.into(), self.accessed_time.into())
    }

    pub(crate) fn first_cluster(&self) -> ClusterId {
        self.first_cluster.into()
    }

    // ── Write-support helpers ───────────────────────────────────────────────

    /// Valid FATX name: 1..=42 bytes, printable ASCII minus reserved chars.
    pub fn is_valid_name(name: &str) -> bool {
        const RESERVED: &[u8] = b"\\/:*?\"<>|";
        let b = name.as_bytes();
        !b.is_empty()
            && b.len() <= FATX_MAX_FILENAME_LEN
            && name != "."
            && name != ".."
            && b.iter().all(|&c| (0x20..0x7F).contains(&c) && !RESERVED.contains(&c))
    }

    /// Fresh entry (file or directory) stamped with the current time.
    pub(crate) fn new_now(name: &str, dir: bool, first_cluster: ClusterId) -> Self {
        let (date, time) = DateTime::now().to_fatx_encoding();
        let mut filename_bytes = [0xFFu8; FATX_MAX_FILENAME_LEN];
        filename_bytes[..name.len()].copy_from_slice(name.as_bytes());
        Self {
            filename_len: name.len() as u8,
            attributes: if dir { FATX_ATTR_DIRECTORY } else { 0 },
            filename_bytes,
            first_cluster: first_cluster.into(),
            file_size: 0.into(),
            modified_time: time.into(),
            modified_date: date.into(),
            created_time: time.into(),
            created_date: date.into(),
            accessed_time: time.into(),
            accessed_date: date.into(),
        }
    }

    pub(crate) fn set_name(&mut self, name: &str) -> Result<(), Error> {
        if !Self::is_valid_name(name) {
            return Err(Error::InvalidName);
        }
        self.filename_bytes = [0xFFu8; FATX_MAX_FILENAME_LEN];
        self.filename_bytes[..name.len()].copy_from_slice(name.as_bytes());
        self.filename_len = name.len() as u8;
        Ok(())
    }

    pub(crate) fn set_first_cluster(&mut self, cluster: ClusterId) {
        self.first_cluster = cluster.into();
    }

    pub(crate) fn set_file_size(&mut self, size: u32) {
        self.file_size = size.into();
    }

    pub(crate) fn touch_modified(&mut self) {
        let (date, time) = DateTime::now().to_fatx_encoding();
        self.modified_date = date.into();
        self.modified_time = time.into();
    }

    /// Scans one directory for `name` (FATX compares case-insensitively).
    /// Returns the entry and its on-disk slot location.
    pub(crate) fn find_in_dir(
        fs: &mut FatxFs,
        dir_cluster: ClusterId,
        name: &str,
    ) -> Result<Option<(Self, DirectoryEntryLocation)>, Error> {
        let mut cluster = dir_cluster;
        let mut guard = 0u64;
        loop {
            for index in 0..fs.num_entries_per_cluster {
                let dirent = fs.read_dirent_at(cluster, index)?;
                match dirent.kind() {
                    DirectoryEntryKind::EndOfDirectory => return Ok(None),
                    DirectoryEntryKind::Deleted => {}
                    DirectoryEntryKind::Valid => {
                        if dirent.file_name().eq_ignore_ascii_case(name) {
                            return Ok(Some((
                                dirent,
                                DirectoryEntryLocation { cluster, index },
                            )));
                        }
                    }
                }
            }
            match fs.fat.entry(cluster)? {
                FatEntry::Data(next) => cluster = next,
                _ => return Ok(None),
            }
            guard += 1;
            if guard > fs.num_clusters as u64 {
                return Err(Error::InvalidClusterChain);
            }
        }
    }
}

pub(crate) struct DirectoryEntryIterator {
    cluster: ClusterId,
    entry: i64,
    finished: bool,
}

impl DirectoryEntryIterator {
    pub(crate) fn new(cluster: ClusterId) -> Self {
        Self {
            cluster,
            entry: -1,
            finished: false,
        }
    }

    pub(crate) fn next(&mut self, fs: &mut FatxFs) -> Option<Result<DirectoryEntry, Error>> {
        if self.finished {
            return None;
        }

        self.entry += 1;

        if (self.entry >= 0) && (self.entry as u64 >= fs.num_entries_per_cluster) {
            // Advance to next cluster
            let fat_entry = fs.fat.entry(self.cluster);
            match fat_entry {
                Err(err) => {
                    self.finished = true;
                    return Some(Err(err));
                }
                Ok(FatEntry::Data(next_cluster)) => {
                    self.cluster = next_cluster as ClusterId;
                    self.entry = 0;
                }
                _ => {
                    self.finished = true;
                    return Some(Err(Error::InvalidClusterChain));
                }
            }
        }

        // Fetch next entry
        if let Err(seek_error) = fs.seek_cluster(
            self.cluster,
            self.entry as u64 * std::mem::size_of::<DirectoryEntry>() as u64,
        ) {
            return Some(Err(seek_error));
        }

        match DirectoryEntry::read_from_io(&mut fs.device_handle) {
            Err(err) => {
                self.finished = true;
                Some(Err(err.into()))
            }
            Ok(entry) => {
                if entry.kind() == DirectoryEntryKind::EndOfDirectory {
                    self.finished = true;
                }
                Some(Ok(entry))
            }
        }
    }
}

pub struct DirectoryEntryIntoIterator {
    pub fs: FatxFsHandle,
    entry_iter: DirectoryEntryIterator,
}

impl DirectoryEntryIntoIterator {
    pub fn new(handle: FatxFsHandle, cluster: ClusterId) -> Self {
        Self {
            fs: handle,
            entry_iter: DirectoryEntryIterator::new(cluster),
        }
    }
}

impl Iterator for DirectoryEntryIntoIterator {
    type Item = Result<DirectoryEntry, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        self.fs.with_lock(|fs| {
            loop {
                // Filter-in only valid directory entry kinds
                let item = self.entry_iter.next(fs);
                if let Some(Ok(dirent)) = item {
                    if let DirectoryEntryKind::Valid = dirent.kind() {
                        return Some(Ok(dirent));
                    } else {
                        continue;
                    }
                }
                return item;
            }
        })
    }
}

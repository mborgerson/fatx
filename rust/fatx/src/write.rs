//! Write support: file/directory creation, deletion, rename, truncate, data
//! writes and free-space reporting.
//!
//! Mirrors the C libfatx write path (`fatx_fat.c`, `fatx_dir.c`, `fatx_file.c`):
//! - every cluster is handed out by a single allocator that fails with an
//!   out-of-space error *before* mutating anything (free space cannot drift
//!   negative: `used = total − free` is always derived from the FAT);
//! - FAT entries and directory slots are persisted write-through;
//! - freshly allocated clusters are zero-filled so grown files never expose
//!   stale device bytes.

use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

use zerocopy::IntoBytes;

use crate::dir::{DirectoryEntry, DirectoryEntryKind, DirectoryEntryLocation};
use crate::error::Error;
use crate::fat::{ClusterId, FatEntry};
use crate::fs::{FatxFs, FatxFsHandle, FATX_FAT_OFFSET_BYTES, FATX_FAT_RESERVED_ENTRIES_COUNT};
use crate::path::normalize_virtual_path;

/// Free-space snapshot returned by [`FatxFsHandle::statfs`].
#[derive(Clone, Copy, Debug)]
pub struct FsStat {
    pub total_clusters: u64,
    pub free_clusters: u64,
    pub bytes_per_cluster: u64,
    pub max_filename_len: u32,
}

impl FatxFs {
    // ── FAT persistence ─────────────────────────────────────────────────────

    /// Writes one FAT entry to memory and to the device.
    pub(crate) fn set_fat_entry(&mut self, index: ClusterId, value: u32) -> Result<(), Error> {
        let width = self.fat.entry_width();
        let pos_in_fat = index as u64 * width as u64;
        self.fat.set_raw(index, value)?;
        let dev_pos = self.partition_offset_bytes + FATX_FAT_OFFSET_BYTES + pos_in_fat;
        self.device_handle.seek(SeekFrom::Start(dev_pos))?;
        match width {
            2 => self
                .device_handle
                .write_all(&(value as u16).to_le_bytes())?,
            _ => self.device_handle.write_all(&value.to_le_bytes())?,
        }
        Ok(())
    }

    /// Allocates one free cluster (EOC-marked, zero-filled). `Error::NoSpace`
    /// when the FAT has no free entry. Scans from a rotating hint like libfatx's
    /// `fatx_alloc_cluster`, but per-filesystem instead of a global static.
    pub(crate) fn alloc_cluster(&mut self) -> Result<ClusterId, Error> {
        let first = FATX_FAT_RESERVED_ENTRIES_COUNT;
        let last = self.num_clusters; // inclusive: clusters are 1..=num_clusters
        let total = last - first + 1;
        let mut idx = self.alloc_hint.clamp(first, last);
        let mut found = None;
        for _ in 0..total {
            if matches!(self.fat.entry(idx)?, FatEntry::Available) {
                found = Some(idx);
                break;
            }
            idx = if idx >= last { first } else { idx + 1 };
        }
        let Some(cluster) = found else {
            return Err(Error::NoSpace);
        };
        self.alloc_hint = if cluster >= last { first } else { cluster + 1 };
        self.set_fat_entry(cluster, self.fat.end_marker())?;

        // Zero-fill so new file/dir content never leaks stale bytes.
        let zeros = vec![0u8; self.num_bytes_per_cluster as usize];
        self.seek_cluster(cluster, 0)?;
        self.device_handle.write_all(&zeros)?;
        Ok(cluster)
    }

    /// Frees a whole cluster chain (no-op when `first` is not a data cluster).
    pub(crate) fn free_cluster_chain(&mut self, first: ClusterId) -> Result<(), Error> {
        let mut cur = first;
        let mut steps = 0u64;
        while cur >= FATX_FAT_RESERVED_ENTRIES_COUNT && cur <= self.num_clusters {
            let next = self.fat.entry(cur)?;
            self.set_fat_entry(cur, 0)?;
            match next {
                FatEntry::Data(n) => cur = n,
                _ => break,
            }
            steps += 1;
            if steps > self.num_clusters as u64 {
                return Err(Error::InvalidClusterChain); // cycle guard
            }
        }
        Ok(())
    }

    /// Number of free clusters (linear scan of the in-memory FAT).
    pub(crate) fn count_free_clusters(&mut self) -> Result<u64, Error> {
        let mut free = 0u64;
        for c in FATX_FAT_RESERVED_ENTRIES_COUNT..=self.num_clusters {
            if matches!(self.fat.entry(c)?, FatEntry::Available) {
                free += 1;
            }
        }
        Ok(free)
    }

    // ── Directory slot mutation ─────────────────────────────────────────────

    /// Persists a directory entry at the given slot location.
    pub(crate) fn write_dirent_at(
        &mut self,
        loc: DirectoryEntryLocation,
        dirent: &DirectoryEntry,
    ) -> Result<(), Error> {
        let entry_size = std::mem::size_of::<DirectoryEntry>() as u64;
        self.seek_cluster(loc.cluster, loc.index * entry_size)?;
        self.device_handle.write_all(dirent.as_bytes())?;
        Ok(())
    }

    /// Marks the slot deleted (0xE5 in the filename-length byte).
    pub(crate) fn mark_dirent_deleted(
        &mut self,
        loc: DirectoryEntryLocation,
    ) -> Result<(), Error> {
        let entry_size = std::mem::size_of::<DirectoryEntry>() as u64;
        self.seek_cluster(loc.cluster, loc.index * entry_size)?;
        self.device_handle.write_all(&[0xE5])?;
        Ok(())
    }

    /// Finds a slot for a new entry in the directory starting at `dir_cluster`:
    /// the first deleted slot, or the end-of-directory position (keeping a
    /// terminator after it, growing the directory by one cluster if needed).
    pub(crate) fn claim_dir_slot(
        &mut self,
        dir_cluster: ClusterId,
    ) -> Result<DirectoryEntryLocation, Error> {
        let entry_size = std::mem::size_of::<DirectoryEntry>() as u64;
        let mut cluster = dir_cluster;
        let mut steps = 0u64;
        loop {
            for index in 0..self.num_entries_per_cluster {
                let dirent = self.read_dirent_at(cluster, index)?;
                match dirent.kind() {
                    DirectoryEntryKind::Deleted => {
                        return Ok(DirectoryEntryLocation { cluster, index });
                    }
                    DirectoryEntryKind::EndOfDirectory => {
                        let loc = DirectoryEntryLocation { cluster, index };
                        // Keep a terminator behind the claimed slot.
                        if index + 1 < self.num_entries_per_cluster {
                            self.seek_cluster(cluster, (index + 1) * entry_size)?;
                            self.device_handle.write_all(&[0xFF])?;
                        } else if !matches!(self.fat.entry(cluster)?, FatEntry::Data(_)) {
                            let grown = self.alloc_cluster()?;
                            self.fill_cluster(grown, 0xFF)?;
                            self.set_fat_entry(cluster, grown)?;
                        }
                        return Ok(loc);
                    }
                    DirectoryEntryKind::Valid => {}
                }
            }
            match self.fat.entry(cluster)? {
                FatEntry::Data(next) => cluster = next,
                _ => {
                    // Chain full of live entries: append a cluster.
                    let grown = self.alloc_cluster()?;
                    self.fill_cluster(grown, 0xFF)?;
                    self.set_fat_entry(cluster, grown)?;
                    return Ok(DirectoryEntryLocation {
                        cluster: grown,
                        index: 0,
                    });
                }
            }
            steps += 1;
            if steps > self.num_clusters as u64 {
                return Err(Error::InvalidClusterChain);
            }
        }
    }

    pub(crate) fn read_dirent_at(
        &mut self,
        cluster: ClusterId,
        index: u64,
    ) -> Result<DirectoryEntry, Error> {
        let entry_size = std::mem::size_of::<DirectoryEntry>() as u64;
        self.seek_cluster(cluster, index * entry_size)?;
        let mut buf = [0u8; std::mem::size_of::<DirectoryEntry>()];
        self.device_handle.read_exact(&mut buf)?;
        Ok(zerocopy::transmute!(buf))
    }

    fn fill_cluster(&mut self, cluster: ClusterId, byte: u8) -> Result<(), Error> {
        let fill = vec![byte; self.num_bytes_per_cluster as usize];
        self.seek_cluster(cluster, 0)?;
        self.device_handle.write_all(&fill)?;
        Ok(())
    }

    // ── Path-level operations (libfatx-style API) ───────────────────────────

    /// Resolves `path` to its parent directory cluster + final component name.
    fn resolve_parent<P: AsRef<Path>>(&mut self, path: P) -> Result<(ClusterId, String), Error> {
        let path = normalize_virtual_path(path);
        let name = path
            .file_name()
            .and_then(|n| n.to_str())
            .ok_or(Error::NotFound)?
            .to_string();
        let parent = path.parent().ok_or(Error::NotFound)?;
        let parent_dirent = DirectoryEntry::from_path(self, parent)?;
        if !parent_dirent.is_directory() {
            return Err(Error::NotADirectory);
        }
        Ok((parent_dirent.first_cluster(), name))
    }

    /// Creates a file (`dir == false`) or directory (`dir == true`).
    pub(crate) fn create_entry(&mut self, path: &str, dir: bool) -> Result<(), Error> {
        let (parent_cluster, name) = self.resolve_parent(path)?;
        if !DirectoryEntry::is_valid_name(&name) {
            return Err(Error::InvalidName);
        }
        if DirectoryEntry::find_in_dir(self, parent_cluster, &name)?.is_some() {
            return Err(Error::AlreadyExists);
        }
        let first_cluster = if dir {
            let c = self.alloc_cluster()?;
            self.fill_cluster(c, 0xFF)?;
            c
        } else {
            0
        };
        let loc = match self.claim_dir_slot(parent_cluster) {
            Ok(loc) => loc,
            Err(e) => {
                if dir {
                    self.free_cluster_chain(first_cluster)?;
                }
                return Err(e);
            }
        };
        let dirent = DirectoryEntry::new_now(&name, dir, first_cluster);
        self.write_dirent_at(loc, &dirent)
    }

    /// Removes a file or an (empty) directory.
    pub(crate) fn remove_entry(&mut self, path: &str, dir: bool) -> Result<(), Error> {
        let (parent_cluster, name) = self.resolve_parent(path)?;
        let (dirent, loc) = DirectoryEntry::find_in_dir(self, parent_cluster, &name)?
            .ok_or(Error::NotFound)?;
        match (dirent.is_directory(), dir) {
            (true, false) => return Err(Error::IsADirectory),
            (false, true) => return Err(Error::NotADirectory),
            (true, true) => {
                if self.dir_has_entries(dirent.first_cluster())? {
                    return Err(Error::DirectoryNotEmpty);
                }
            }
            (false, false) => {}
        }
        self.mark_dirent_deleted(loc)?;
        let first = dirent.first_cluster();
        if first != 0 {
            self.free_cluster_chain(first)?;
        }
        Ok(())
    }

    fn dir_has_entries(&mut self, dir_cluster: ClusterId) -> Result<bool, Error> {
        let mut cluster = dir_cluster;
        loop {
            for index in 0..self.num_entries_per_cluster {
                match self.read_dirent_at(cluster, index)?.kind() {
                    DirectoryEntryKind::Valid => return Ok(true),
                    DirectoryEntryKind::EndOfDirectory => return Ok(false),
                    DirectoryEntryKind::Deleted => {}
                }
            }
            match self.fat.entry(cluster)? {
                FatEntry::Data(next) => cluster = next,
                _ => return Ok(false),
            }
        }
    }

    /// Renames/moves an entry (replacing an existing target file, POSIX-style).
    pub(crate) fn rename_entry(&mut self, from: &str, to: &str) -> Result<(), Error> {
        let (from_parent, from_name) = self.resolve_parent(from)?;
        let (to_parent, to_name) = self.resolve_parent(to)?;
        if !DirectoryEntry::is_valid_name(&to_name) {
            return Err(Error::InvalidName);
        }
        let (mut dirent, from_loc) = DirectoryEntry::find_in_dir(self, from_parent, &from_name)?
            .ok_or(Error::NotFound)?;
        if let Some((existing, _)) = DirectoryEntry::find_in_dir(self, to_parent, &to_name)? {
            if existing.is_directory() {
                return Err(Error::IsADirectory);
            }
            self.remove_entry(to, false)?;
        }
        let to_loc = self.claim_dir_slot(to_parent)?;
        dirent.set_name(&to_name)?;
        self.write_dirent_at(to_loc, &dirent)?;
        self.mark_dirent_deleted(from_loc)
    }

    /// Sets file size; grows (zero-filled, `NoSpace`-safe) or shrinks the chain.
    pub(crate) fn truncate_path(&mut self, path: &str, new_size: u32) -> Result<(), Error> {
        let (parent_cluster, name) = self.resolve_parent(path)?;
        let (mut dirent, loc) = DirectoryEntry::find_in_dir(self, parent_cluster, &name)?
            .ok_or(Error::NotFound)?;
        if dirent.is_directory() {
            return Err(Error::IsADirectory);
        }
        let csize = self.num_bytes_per_cluster;
        let need = (new_size as u64).div_ceil(csize);
        let chain = self.chain_of(dirent.first_cluster())?;
        let have = chain.len() as u64;

        if need > have {
            if need - have > self.count_free_clusters()? {
                return Err(Error::NoSpace);
            }
            let mut last = chain.last().copied();
            for _ in 0..(need - have) {
                let c = self.alloc_cluster()?;
                match last {
                    Some(prev) => self.set_fat_entry(prev, c)?,
                    None => dirent.set_first_cluster(c),
                }
                last = Some(c);
            }
        } else if need < have {
            if need == 0 {
                self.free_cluster_chain(dirent.first_cluster())?;
                dirent.set_first_cluster(0);
            } else {
                self.set_fat_entry(chain[need as usize - 1], self.fat.end_marker())?;
                for &c in &chain[need as usize..] {
                    self.set_fat_entry(c, 0)?;
                }
            }
        }
        // Zero the tail of the last kept cluster on shrink (stale-byte hygiene).
        let old_size = dirent.file_size();
        if new_size < old_size && new_size > 0 {
            let within = new_size as u64 % csize;
            if within != 0 {
                let chain = self.chain_of(dirent.first_cluster())?;
                if let Some(&lastc) = chain.last() {
                    let zeros = vec![0u8; (csize - within) as usize];
                    self.seek_cluster(lastc, within)?;
                    self.device_handle.write_all(&zeros)?;
                }
            }
        }
        dirent.set_file_size(new_size);
        dirent.touch_modified();
        self.write_dirent_at(loc, &dirent)
    }

    /// Writes `data` at byte `offset`, growing the file as needed. All-or-
    /// nothing with respect to space (`NoSpace` before any mutation on grow).
    pub(crate) fn write_path(
        &mut self,
        path: &str,
        offset: u64,
        data: &[u8],
    ) -> Result<usize, Error> {
        if data.is_empty() {
            return Ok(0);
        }
        let end = offset + data.len() as u64;
        if end > u32::MAX as u64 {
            return Err(Error::FileTooLarge); // FATX file-size cap: 4 GiB − 1
        }
        // Grow first when needed (checks space, allocates, persists size).
        {
            let (parent_cluster, name) = self.resolve_parent(path)?;
            let (dirent, _) = DirectoryEntry::find_in_dir(self, parent_cluster, &name)?
                .ok_or(Error::NotFound)?;
            if dirent.is_directory() {
                return Err(Error::IsADirectory);
            }
            if end > dirent.file_size() as u64 {
                self.truncate_path(path, end as u32)?;
            }
        }
        let (parent_cluster, name) = self.resolve_parent(path)?;
        let (dirent, _) =
            DirectoryEntry::find_in_dir(self, parent_cluster, &name)?.ok_or(Error::NotFound)?;
        let csize = self.num_bytes_per_cluster;
        let chain = self.chain_of(dirent.first_cluster())?;
        let mut done = 0usize;
        while done < data.len() {
            let pos = offset + done as u64;
            let ci = (pos / csize) as usize;
            let within = pos % csize;
            let n = ((csize - within) as usize).min(data.len() - done);
            let cluster = *chain.get(ci).ok_or(Error::InvalidClusterChain)?;
            self.seek_cluster(cluster, within)?;
            self.device_handle.write_all(&data[done..done + n])?;
            done += n;
        }
        Ok(done)
    }

    /// Collects the cluster chain starting at `first` (empty for 0).
    pub(crate) fn chain_of(&mut self, first: ClusterId) -> Result<Vec<ClusterId>, Error> {
        let mut out = Vec::new();
        let mut cur = first;
        while cur >= FATX_FAT_RESERVED_ENTRIES_COUNT && cur <= self.num_clusters {
            out.push(cur);
            match self.fat.entry(cur)? {
                FatEntry::Data(n) => cur = n,
                FatEntry::End | FatEntry::Media => break,
                _ => break,
            }
            if out.len() as u64 > self.num_clusters as u64 {
                return Err(Error::InvalidClusterChain);
            }
        }
        Ok(out)
    }
}

impl FatxFsHandle {
    pub fn create_file(&mut self, path: &str) -> Result<(), Error> {
        self.with_lock(|fs| fs.create_entry(path, false))
    }
    pub fn mkdir(&mut self, path: &str) -> Result<(), Error> {
        self.with_lock(|fs| fs.create_entry(path, true))
    }
    pub fn unlink(&mut self, path: &str) -> Result<(), Error> {
        self.with_lock(|fs| fs.remove_entry(path, false))
    }
    pub fn rmdir(&mut self, path: &str) -> Result<(), Error> {
        self.with_lock(|fs| fs.remove_entry(path, true))
    }
    pub fn rename(&mut self, from: &str, to: &str) -> Result<(), Error> {
        self.with_lock(|fs| fs.rename_entry(from, to))
    }
    pub fn truncate(&mut self, path: &str, size: u32) -> Result<(), Error> {
        self.with_lock(|fs| fs.truncate_path(path, size))
    }
    pub fn write(&mut self, path: &str, offset: u64, data: &[u8]) -> Result<usize, Error> {
        self.with_lock(|fs| fs.write_path(path, offset, data))
    }
    /// Free-space snapshot; never inconsistent (derived from the FAT).
    pub fn statfs(&mut self) -> Result<FsStat, Error> {
        self.with_lock(|fs| {
            Ok(FsStat {
                total_clusters: fs.num_clusters as u64,
                free_clusters: fs.count_free_clusters()?,
                bytes_per_cluster: fs.num_bytes_per_cluster,
                max_filename_len: 42,
            })
        })
    }
    pub fn sync(&mut self) -> Result<(), Error> {
        self.with_lock(|fs| fs.device_handle.sync_all().map_err(Error::Io))
    }
}

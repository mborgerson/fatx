use std::cmp::min;
use std::io;

use crate::dir::DirectoryEntry;
use crate::fat::FatEntry;
use crate::fs::FatxFsHandle;

pub struct File {
    handle: FatxFsHandle,
    dirent: DirectoryEntry,
    seek_pos: u32,
}

impl File {
    pub fn new(handle: FatxFsHandle, dirent: DirectoryEntry) -> Self {
        Self {
            handle,
            dirent,
            seek_pos: 0,
        }
    }

    pub fn file_size(&self) -> u32 {
        self.dirent.file_size()
    }
}

impl io::Seek for File {
    fn seek(&mut self, pos: io::SeekFrom) -> io::Result<u64> {
        match pos {
            io::SeekFrom::Start(offset) => {
                self.seek_pos = offset as u32;
            }
            io::SeekFrom::Current(offset) => {
                let target = (self.seek_pos as i64).saturating_add(offset);
                if target < 0 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "Seek target cannot be negative",
                    ));
                }
                self.seek_pos = target as u32;
            }
            io::SeekFrom::End(offset) => {
                let target = self.dirent.file_size() as i64 + offset;
                if target < 0 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "Seek target cannot be negative",
                    ));
                }
                self.seek_pos = target as u32;
            }
        }
        Ok(self.seek_pos as u64)
    }
}

impl io::Read for File {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        // Get handle to filesystem interface
        let handle = self.handle.fs.clone();
        let mut fs = handle.lock().unwrap();

        let file_size: u32 = self.dirent.file_size();
        let mut buf_pos: u64 = 0;

        while buf_pos < buf.len() as u64 {
            // Ensure we don't read past EOF
            let bytes_remaining_in_file: u32 = file_size - self.seek_pos;
            if bytes_remaining_in_file == 0 {
                break;
            }

            // Map current file position to cluster number
            let cluster = {
                let mut cluster: u32 = self.dirent.first_cluster();
                let count = (self.seek_pos as u64 / fs.num_bytes_per_cluster) as u32;
                for _ in 0..count {
                    cluster = match fs.fat.entry(cluster)? {
                        FatEntry::Data(cluster) => cluster,
                        _ => return Err(io::Error::other("Corrupt FAT chain")),
                    };
                }
                cluster
            };

            // Determine number of bytes to read in this cluster
            let byte_offset_in_cluster = self.seek_pos as u64 % fs.num_bytes_per_cluster;
            let bytes_remaining_in_cluster = fs.num_bytes_per_cluster - byte_offset_in_cluster;
            let bytes_remaining_in_dest = buf.len() as u64 - buf_pos;
            let bytes_to_read_from_cluster = min(
                min(bytes_remaining_in_dest, bytes_remaining_in_cluster),
                bytes_remaining_in_file as u64,
            );

            // Read cluster data from device
            log::debug!(
                "Reading {} bytes from cluster {}",
                bytes_to_read_from_cluster,
                cluster
            );
            fs.seek_cluster(cluster, byte_offset_in_cluster)?;
            let bytes_read = {
                let start = buf_pos as usize;
                let end = start + bytes_to_read_from_cluster as usize;
                fs.device_handle.read(&mut buf[start..end])?
            };

            // Advance
            buf_pos += bytes_read as u64;
            self.seek_pos += bytes_read as u32;
        }

        Ok(buf_pos as usize)
    }
}

use std::io;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum Error {
    #[error("io error")]
    Io(#[from] io::Error),
    #[error("the filesystem did not have the expected signature")]
    InvalidFilesystemSignature,
    #[error("the partition offset is invalid")]
    InvalidPartitionOffset,
    #[error("the partition size is invalid")]
    InvalidPartitionSize,
    #[error("the number of sectors per cluster is invalid")]
    InvalidSectorsPerCluster,
    #[error("the root cluster is invalid")]
    InvalidRootCluster,
    #[error("the cluster number is invalid")]
    InvalidClusterNumber,
    #[error("the cluster chain is corrupt")]
    InvalidClusterChain,
    #[error("the desired item could not be found")]
    NotFound,
    #[error("one path component is not a directory")]
    NotADirectory,
    #[error("the path unxpectedly identifies a directory")]
    IsADirectory,
    #[error("no space left on the volume")]
    NoSpace,
    #[error("the name is not a valid FATX filename")]
    InvalidName,
    #[error("an entry with this name already exists")]
    AlreadyExists,
    #[error("the directory is not empty")]
    DirectoryNotEmpty,
    #[error("the file would exceed the FATX size limit (4 GiB - 1)")]
    FileTooLarge,
    #[error("the filesystem is mounted read-only")]
    ReadOnly,
}

impl From<Error> for io::Error {
    fn from(err: Error) -> Self {
        match err {
            Error::Io(err) => err,
            Error::NotFound => io::Error::new(io::ErrorKind::NotFound, err),
            Error::NotADirectory => io::Error::new(io::ErrorKind::NotADirectory, err),
            Error::IsADirectory => io::Error::new(io::ErrorKind::IsADirectory, err),
            Error::NoSpace => io::Error::new(io::ErrorKind::StorageFull, err),
            Error::InvalidName => io::Error::new(io::ErrorKind::InvalidInput, err),
            Error::AlreadyExists => io::Error::new(io::ErrorKind::AlreadyExists, err),
            Error::DirectoryNotEmpty => io::Error::new(io::ErrorKind::DirectoryNotEmpty, err),
            Error::FileTooLarge => io::Error::new(io::ErrorKind::FileTooLarge, err),
            Error::ReadOnly => io::Error::new(io::ErrorKind::ReadOnlyFilesystem, err),
            _ => io::Error::other(err),
        }
    }
}

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
}

impl From<Error> for io::Error {
    fn from(err: Error) -> Self {
        match err {
            Error::Io(err) => err,
            Error::NotFound => io::Error::new(io::ErrorKind::NotFound, err),
            Error::NotADirectory => io::Error::new(io::ErrorKind::NotADirectory, err),
            Error::IsADirectory => io::Error::new(io::ErrorKind::IsADirectory, err),
            _ => io::Error::other(err),
        }
    }
}

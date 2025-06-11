use chrono::NaiveDate;
use std::ffi::OsStr;
use std::io::{Read, Seek};
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::{Duration, SystemTime};

use bimap::BiMap;
use clap::Parser;
use clap::builder::styling::{AnsiColor, Effects, Style, Styles};
use fatx::{DirectoryEntry, FatxFs, FatxFsConfig, FatxFsHandle};
use fuser::{
    FileAttr, FileType, Filesystem, MountOption, ReplyAttr, ReplyData, ReplyDirectory, ReplyEntry,
    Request,
};
use libc::ENOENT;

type Inode = u64;

struct InodeTracker {
    bimap: Mutex<BiMap<Inode, String>>,
    next_inode: Mutex<Inode>,
}

impl InodeTracker {
    fn new() -> Self {
        let mut bimap = BiMap::new();
        bimap.insert(1, String::from("/")); // root
        Self {
            bimap: Mutex::new(bimap),
            next_inode: Mutex::new(2),
        }
    }

    fn get_or_create_inode(&self, path: &str) -> Inode {
        let mut bimap = self.bimap.lock().unwrap();
        if let Some(inode) = bimap.get_by_right(path) {
            return *inode;
        }

        let mut inode_counter = self.next_inode.lock().unwrap();
        let inode = *inode_counter;
        *inode_counter += 1;

        bimap.insert(inode, path.to_string());
        inode
    }

    fn get_path(&self, inode: Inode) -> Option<String> {
        let bimap = self.bimap.lock().unwrap();
        bimap.get_by_left(&inode).cloned()
    }
}

struct FuseFatxFs {
    fatx: FatxFsHandle,
    inodes: InodeTracker,
}

fn fatx_datetime_to_systemtime(datetime: fatx::DateTime) -> SystemTime {
    if let Some(date) = NaiveDate::from_ymd_opt(
        datetime.year().into(),
        datetime.month().into(),
        datetime.day().into(),
    ) {
        if let Some(datetime) = date.and_hms_opt(
            datetime.hour().into(),
            datetime.minute().into(),
            datetime.second().into(),
        ) {
            return SystemTime::from(datetime.and_utc());
        }
    }

    // Failed to convert datetime. Supply default.
    let datetime = NaiveDate::from_ymd_opt(2000, 1, 1)
        .unwrap()
        .and_hms_opt(0, 0, 0)
        .unwrap();
    SystemTime::from(datetime.and_utc())
}

impl FuseFatxFs {
    fn dirent_to_attr(&mut self, inode: u64, dirent: &DirectoryEntry) -> Option<FileAttr> {
        if dirent.is_directory() {
            return Some(FileAttr {
                ino: inode,
                size: 0,
                blocks: 0,
                atime: fatx_datetime_to_systemtime(dirent.accessed()),
                mtime: fatx_datetime_to_systemtime(dirent.modified()),
                ctime: fatx_datetime_to_systemtime(dirent.modified()),
                crtime: fatx_datetime_to_systemtime(dirent.created()),
                kind: FileType::Directory,
                perm: 0o755,
                nlink: 2,
                uid: 501,
                gid: 20,
                rdev: 0,
                flags: 0,
                blksize: 1,
            });
        }

        if dirent.is_file() {
            return Some(FileAttr {
                ino: inode,
                size: dirent.file_size() as u64,
                blocks: (dirent.file_size() / 512) as u64, // FIXME: num clusters?
                atime: fatx_datetime_to_systemtime(dirent.accessed()),
                mtime: fatx_datetime_to_systemtime(dirent.modified()),
                ctime: fatx_datetime_to_systemtime(dirent.modified()),
                crtime: fatx_datetime_to_systemtime(dirent.created()),
                kind: FileType::RegularFile,
                perm: 0o644, // FIXME
                nlink: 1,
                uid: 501,
                gid: 20,
                rdev: 0,
                flags: 0,
                blksize: 512,
            });
        }

        None
    }
}

const TTL: Duration = Duration::from_secs(1);

impl Filesystem for FuseFatxFs {
    fn lookup(&mut self, _req: &Request, parent: u64, name: &OsStr, reply: ReplyEntry) {
        log::debug!("lookup({}, {:?})", parent, name);
        if let Some(root) = self.inodes.get_path(parent) {
            // Construct combined path
            let mut path = PathBuf::from(root);
            path.push(name.to_str().unwrap());
            let path_str = path.to_str().unwrap();

            if let Ok(dirent) = self.fatx.stat(path_str) {
                let inode = self.inodes.get_or_create_inode(path_str);
                if let Some(attr) = self.dirent_to_attr(inode, &dirent) {
                    reply.entry(&TTL, &attr, 0);
                    return;
                }
            }
        }
        reply.error(ENOENT);
    }

    fn getattr(&mut self, _req: &Request, ino: u64, _fh: Option<u64>, reply: ReplyAttr) {
        log::debug!("getattr({})", ino);
        if let Some(path) = self.inodes.get_path(ino) {
            let dirent = self.fatx.stat(&path).expect("failed to stat");
            if let Some(attr) = self.dirent_to_attr(ino, &dirent) {
                reply.attr(&TTL, &attr);
                return;
            }
        }
        reply.error(ENOENT);
    }

    fn read(
        &mut self,
        _req: &Request,
        ino: u64,
        _fh: u64,
        offset: i64,
        _size: u32,
        _flags: i32,
        _lock: Option<u64>,
        reply: ReplyData,
    ) {
        log::debug!("read({})", ino);
        if let Some(path) = self.inodes.get_path(ino) {
            let mut file = self.fatx.open(&path).unwrap();
            file.seek(std::io::SeekFrom::Start(offset as u64))
                .expect("failed to seek");

            let mut data = vec![0u8; _size as usize];
            _ = file.read(&mut data).unwrap();

            reply.data(&data);
            return;
        }

        reply.error(ENOENT);
    }

    fn readdir(
        &mut self,
        _req: &Request,
        ino: u64,
        _fh: u64,
        offset: i64,
        mut reply: ReplyDirectory,
    ) {
        log::debug!("readdir({}, offset={})", ino, offset);

        if let Some(dir_path_str) = self.inodes.get_path(ino) {
            let dir_iter = self.fatx.read_dir(&dir_path_str).expect("read_dir failed");
            let dir_path = PathBuf::from(dir_path_str);

            // Add self entry (.)
            let mut entries = vec![(ino, FileType::Directory, String::from("."))];

            // Add parent entry (..)
            if ino == 1 {
                entries.push((1, FileType::Directory, String::from("..")));
            } else {
                let parent_path = dir_path.parent().unwrap_or(&dir_path);
                let parent_path_str = parent_path.to_str().unwrap();
                let parent_inode = self.inodes.get_or_create_inode(parent_path_str);
                entries.push((parent_inode, FileType::Directory, String::from("..")));
            }

            // Add directory entries
            for dirent in dir_iter.flatten() {
                if dirent.is_file() || dirent.is_directory() {
                    let mut child_path = dir_path.clone();
                    child_path.push(dirent.file_name());
                    let child_path_str = child_path.to_str().unwrap();

                    let child_inode = self.inodes.get_or_create_inode(child_path_str);

                    let ftype = if dirent.is_file() {
                        FileType::RegularFile
                    } else {
                        FileType::Directory
                    };

                    entries.push((child_inode, ftype, dirent.file_name()))
                }
            }

            // Reply with desired entries
            for (i, entry) in entries.into_iter().enumerate().skip(offset as usize) {
                // i + 1 means the index of the next entry
                if reply.add(entry.0, (i + 1) as i64, entry.1, entry.2) {
                    break;
                }
            }
            reply.ok();
        }
    }
}

const HEADER: Style = AnsiColor::Green.on_default().effects(Effects::BOLD);
const USAGE: Style = AnsiColor::Green.on_default().effects(Effects::BOLD);
const LITERAL: Style = AnsiColor::Cyan.on_default().effects(Effects::BOLD);
const PLACEHOLDER: Style = AnsiColor::Cyan.on_default();
const ERROR: Style = AnsiColor::Red.on_default().effects(Effects::BOLD);
const VALID: Style = AnsiColor::Cyan.on_default().effects(Effects::BOLD);
const INVALID: Style = AnsiColor::Yellow.on_default().effects(Effects::BOLD);

/// Cargo's color style
/// [source](https://github.com/crate-ci/clap-cargo/blob/master/src/style.rs)
const CARGO_STYLING: Styles = Styles::styled()
    .header(HEADER)
    .usage(USAGE)
    .literal(LITERAL)
    .placeholder(PLACEHOLDER)
    .error(ERROR)
    .valid(VALID)
    .invalid(INVALID);

#[derive(Parser, Debug)]
#[command(version, about, long_about = None)]
#[clap(styles = CARGO_STYLING)]
struct Cli {
    /// Device file containing FATX partition to mount
    #[arg()]
    device_path: String,

    /// FUSE filesystem mount point
    #[arg()]
    mount_point: String,

    /// Drive letter of c|e|x|y|z
    #[arg(short, long, default_value_t = String::from("c"))]
    drive_letter: String,

    /// Auto-unmount
    #[arg(long)]
    auto_unmount: bool,

    /// Allow root
    #[arg(long)]
    allow_root: bool,
}

fn main() {
    env_logger::init();
    let cli = Cli::parse();
    let mut options = vec![MountOption::RO, MountOption::FSName("fatx".to_string())];
    if cli.auto_unmount {
        options.push(MountOption::AutoUnmount);
    }
    if cli.allow_root {
        options.push(MountOption::AllowRoot);
    }

    let config = FatxFsConfig::new(cli.device_path).drive_letter(&cli.drive_letter);

    let fs = FuseFatxFs {
        fatx: FatxFs::open_device(&config).unwrap(),
        inodes: InodeTracker::new(),
    };
    fuser::mount2(fs, cli.mount_point, &options).unwrap();
}

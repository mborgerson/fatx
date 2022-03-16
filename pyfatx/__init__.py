#!/usr/bin/env python3
import os
import platform
import subprocess
import logging
from typing import AnyStr, Generator, Optional, Sequence, Tuple

from .libfatx import ffi
from .libfatx.lib import *


log = logging.getLogger(__name__)


class FatxAttr:
	"""
	FATX File Attribute Structure
	"""

	def __init__(self, filename: str, attributes: int, filesize: int):
		self.filename = filename
		self.attributes = attributes
		self.file_size = filesize

	@property
	def is_readonly(self): return self.attributes & (1<<0)

	@property
	def is_system(self): return self.attributes & (1<<1)

	@property
	def is_hidden(self): return self.attributes & (1<<2)

	@property
	def is_volume(self): return self.attributes & (1<<3)

	@property
	def is_directory(self): return self.attributes & (1<<4)

	@property
	def is_file(self): return not self.is_directory

	def __repr__(self):
		attr_desc = ','.join(
			  ['dir'  if self.is_directory else 'file']
			+ ['ro']  if self.is_readonly else []
			+ ['sys'] if self.is_system   else []
			+ ['hid'] if self.is_hidden   else []
			+ ['vol'] if self.is_volume   else []
			)
		return f'<FatxAttr name={self.filename} attr={attr_desc} size={self.file_size:#x}>'


class Fatx:
	"""
	FATX Filesystem Interface
	"""

	def __init__(self, path: str, offset: Optional[int] = None, size: Optional[int] = None, drive: str = 'c',
		         sector_size: int = 512):
		self.fs = pyfatx_open_helper()
		assert self.fs
		if offset is None:
			partitions = {
				'x': (0x00080000, 0x02ee00000),
				'y': (0x2ee80000, 0x02ee00000),
				'z': (0x5dc80000, 0x02ee00000),
				'c': (0x8ca80000, 0x01f400000),
				'e': (0xabe80000, 0x1312d6000),
			}
			offset, size = partitions[drive]
		if isinstance(path, str):
			path = path.encode('utf-8')
		s = fatx_open_device(self.fs, path, offset, size, sector_size, 0)
		if s != 0:
			self.fs = None
		assert s == 0

	def __del__(self):
		if self.fs is not None:
			fatx_close_device(self.fs)
			# FIXME: Leaks fs

	def _sanitize_path(self, path):
		if isinstance(path, str):
			path = path.encode('utf-8')
		if not path.startswith(b'/'):
			path = b'/' + path
		path = path.replace(b'\\', b'/')
		return path

	def _create_attr(self, in_attr):
		fname = ffi.string(in_attr.filename).decode('ascii')
		return FatxAttr(fname, in_attr.attributes, in_attr.file_size)

	def get_attr(self, path: AnyStr) -> FatxAttr:
		"""
		Get file attributes for a given path.
		"""
		path = self._sanitize_path(path)
		attr = ffi.new('struct fatx_attr *')
		s = fatx_get_attr(self.fs, path, attr)
		assert s == 0
		return self._create_attr(attr)

	def listdir(self, path: str) -> Generator[FatxAttr, None, None]:
		"""
		List the files in a directory.
		"""
		path = self._sanitize_path(path)
		d = ffi.new('struct fatx_dir *')
		s = fatx_open_dir(self.fs, path, d)
		assert s == 0

		dirent = ffi.new('struct fatx_dirent *')
		attr = ffi.new('struct fatx_attr *')
		next_dirent = ffi.new('struct fatx_dirent **')

		while True:
			s = fatx_read_dir(self.fs, d, dirent, attr, next_dirent)
			if s != 0:
				break

			yield self._create_attr(attr)

			s = fatx_next_dir_entry(self.fs, d)
			if s != 0:
				break

		s = fatx_close_dir(self.fs, d)
		assert s == 0

	def walk(self, path: str) -> Generator[Tuple[str, Sequence[FatxAttr], Sequence[FatxAttr]], None, None]:
		"""
		Walk the filesystem.
		"""
		attrs = list(self.listdir(path))
		dirnames = [d.filename for d in attrs if d.is_directory]
		filenames = [f.filename for f in attrs if f.is_file]
		yield (path, dirnames, filenames)
		for d in dirnames:
			yield from self.walk(os.path.join(path, d))

	def read(self, path: str, offset: int = 0, size: Optional[int] = None) -> bytes:
		"""
		Read a file.
		"""
		path = self._sanitize_path(path)
		attr = self.get_attr(path)
		assert(attr.is_file)
		assert(offset < attr.file_size)
		if size is None:
			size = attr.file_size - offset
		if size == 0:
			return b''
		buf = ffi.new(f'char[{attr.file_size}]')
		s = fatx_read(self.fs, path, offset, size, buf)
		assert s == size
		return ffi.buffer(buf)

	def create_dirent(self, path: AnyStr, attributes: int = 0):
		"""
		Creates an empty file.
		"""
		dir_name = self._sanitize_path(os.path.dirname(path))
		assert dir_name

		d = ffi.new('struct fatx_dir *')
		s = fatx_open_dir(self.fs, dir_name, d)
		assert s == 0

		path = self._sanitize_path(path)
		s = fatx_create_dirent(self.fs, path, d, attributes)
		if s:
			fatx_close_dir(self.fs, d)
			assert s == 0

		s = fatx_close_dir(self.fs, d)
		assert s == 0

	def write(self, path: str, content: bytes, offset: int = 0, new_file_attributes: int = 0):
		"""
		Write to a file.
		"""
		path = self._sanitize_path(path)
		try:
			attr = self.get_attr(path)
		except AssertionError:
			self.create_dirent(path, new_file_attributes)
			attr = self.get_attr(path)

		if attr.is_file:
			assert not attr.is_readonly
		else:
			assert not attr.is_directory

		s = fatx_write(self.fs, path, offset, len(content), content)
		assert s == len(content)

	def rename(self, current_name: str, new_name: str):
		"""
		Rename an existing file.

		`current_name` must exist and be a file.
		`new_name` must be a valid filename and must be in the same directory as
		`current_name`.
		"""
		from_path = self._sanitize_path(current_name)
		attr = self.get_attr(from_path)
		# Only files are currently supported by libfatx.
		assert attr.is_file

		to_path = self._sanitize_path(new_name)
		s = fatx_rename(self.fs, from_path, to_path)
		assert s == 0

	def truncate(self, path: AnyStr, new_size: int):
		"""
		Truncate an existing file to the given size.
		"""
		path = self._sanitize_path(path)
		attr = self.get_attr(path)
		assert attr.is_file
		s = fatx_truncate(self.fs, path, new_size)
		assert s == 0

	def unlink(self, path: AnyStr):
		"""
		Delete an existing file.
		"""
		path = self._sanitize_path(path)
		s = fatx_unlink(self.fs, path)
		assert s == 0

	@classmethod
	def format(cls, path:str):
		"""
		Format a device.
		"""
		log.info('Formatting...')
		fs = pyfatx_open_helper()
		s = fatx_disk_format(fs, path.encode('utf-8'), 512, 1, 128)
		# FIXME: Leaks fs
		assert s == 0

	@classmethod
	def create(cls, path:str, size:int = 8*1024*1024*1024):
		"""
		Create a disk image.
		"""
		if os.path.exists(path):
			raise FileExistsError()

		log.info('Creating empty disk image at %s...', path)
		plat = platform.system()
		if plat == 'Windows':
			subprocess.run(['fsutil', 'file', 'createnew', path, str(size)], check=True)
		elif plat == 'Linux':
			subprocess.run(['fallocate', '-l', str(size), path])
		elif plat == 'Darwin':
			subprocess.run(['mkfile', '-n', str(size), path])
		else:
			assert False

		Fatx.format(path)

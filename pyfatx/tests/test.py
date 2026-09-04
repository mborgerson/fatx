#!/usr/bin/env python3
import functools
import importlib.util
import os
import tempfile
import unittest
import random

from pyfatx import Fatx

X360_IMAGE_BUILDER = os.path.join(
	os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tests', 'make_x360_image.py')


def with_formatted_disk(func):
	@functools.wraps(func)
	def wrapper(*args, **kwargs):
		with tempfile.NamedTemporaryFile(delete=False) as tmp_file:
			hdd_img_path = tmp_file.name + "-fatx"
		try:
			Fatx.create(hdd_img_path)
			return func(*args, hdd_img_path, **kwargs)
		finally:
			os.remove(hdd_img_path)
			os.remove(tmp_file.name)
	return wrapper


class BasicTest(unittest.TestCase):
	"""
	Run basic tests.
	"""

	@with_formatted_disk
	def test_read_empty_file(self, path):
		fs = Fatx(path)
		empty_file = '/empty'
		fs.mknod(empty_file)

		res = fs.read(empty_file)
		fs.unlink(empty_file)
		assert len(res) == 0

	@with_formatted_disk
	def test_create_file(self, path):
		test_file_path = '/test_file.txt'
		fs = Fatx(path)
		content = b'12345'
		fs.write(test_file_path, content)

		written = fs.read(test_file_path)
		assert written == content

		fs.unlink(test_file_path)
		file_still_available = False
		try:
			fs.get_attr(test_file_path)
			file_still_available = True
		except AssertionError:
			pass
		assert not file_still_available

	@with_formatted_disk
	def test_truncate_file(self, path):
		test_file_path = '/test_file.txt'
		fs = Fatx(path)
		content = b'12345'
		fs.write(test_file_path, content)

		fs.truncate(test_file_path, 3)

		written = fs.read(test_file_path)
		assert written == content[:3]

		fs.unlink(test_file_path)

	@with_formatted_disk
	def test_rename_file(self, path):
		test_file_path = '/test_file.txt'
		fs = Fatx(path)
		content = b'12345'
		fs.write(test_file_path, content)

		new_filename = '/renamed_test_file.txt'
		fs.rename(test_file_path, new_filename)

		original_file_still_available = False
		try:
			fs.get_attr(test_file_path)
			original_file_still_available = True
		except AssertionError:
			pass
		assert not original_file_still_available

		fs.unlink(new_filename)

	@with_formatted_disk
	def test_write_large_file(self, path):
		test_file_path = '/largefile'
		fs = Fatx(path)

		rng = random.Random()
		rng.seed(12345)
		b = bytes([rng.getrandbits(8) for _ in range(1024 * 1024)])
		
		fs.write(test_file_path, b)

		d = fs.read(test_file_path)
		fs.unlink(test_file_path)

		assert d == b

	
	@with_formatted_disk
	def test_write_offset(self, path):
		test_file_path = '/offsetfile'
		fs = Fatx(path)

		rng = random.Random()
		rng.seed(12345)

		# Write 1KB of random data
		b = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file_path, b)

		# Delete the data
		d = fs.read(test_file_path)
		fs.unlink(test_file_path)
		assert d == b

		# Write 512KB of random data at offset 128
		b = bytes([rng.getrandbits(8) for _ in range(1024 * 512)])
		fs.write(test_file_path, b, offset=128)

		# Read from offset zero
		d = fs.read(test_file_path)
		fs.unlink(test_file_path)

		assert d == bytes([0] * 128) + b


	@with_formatted_disk
	def test_rename_overwrite(self, path):
		test_file1 = '/test_overwrite1'
		test_file2 = '/test_overwrite2'
		fs = Fatx(path)

		rng = random.Random()
		rng.seed(12345)

		b1 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file1, b1)

		b2 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file2, b2)

		# file1 should be above file2 in the dirent list
		# so this tests that overwriting file1 removes it
		fs.rename(test_file2, test_file1)

		d = fs.read(test_file1)
		fs.unlink(test_file1)

		original_file_still_available = False
		try:
			fs.get_attr(test_file2)
			original_file_still_available = True
		except AssertionError:
			pass
		assert not original_file_still_available

		assert d == b2


	@with_formatted_disk
	def test_rename_exchange(self, path):
		test_file1 = '/test_xchg1'
		test_file2 = '/test_xchg2'
		fs = Fatx(path)

		rng = random.Random()
		rng.seed(12345)

		b1 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file1, b1)

		b2 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file2, b2)

		fs.rename(test_file2, test_file1, exchange=True)

		d1 = fs.read(test_file1)
		d2 = fs.read(test_file2)

		fs.unlink(test_file1)
		fs.unlink(test_file2)

		assert d1 == b2
		assert d2 == b1


	@with_formatted_disk
	def test_rename_exchange_different_dirname_overwrite(self, path):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx(path)

		fs.mkdir(file2_dir)

		rng = random.Random()
		rng.seed(12345)

		b1 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file1, b1)

		b2 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file2, b2)

		fs.rename(test_file2, test_file1, exchange=True)

		d1 = fs.read(test_file1)
		d2 = fs.read(test_file2)

		fs.unlink(test_file1)
		fs.unlink(test_file2)
		
		fs.rmdir(file2_dir)

		assert d1 == b2
		assert d2 == b1


	@with_formatted_disk
	def test_rename_different_dirname_overwrite(self, path):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx(path)

		fs.mkdir(file2_dir)

		rng = random.Random()
		rng.seed(12345)

		b1 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file1, b1)

		b2 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file2, b2)

		fs.rename(test_file2, test_file1)

		d1 = fs.read(test_file1)
		fs.unlink(test_file1)

		fs.rmdir(file2_dir)

		original_file_still_available = False
		try:
			fs.get_attr(test_file2)
			original_file_still_available = True
		except AssertionError:
			pass
		assert not original_file_still_available

		assert d1 == b2


	@with_formatted_disk
	def test_rename_different_dirname_nonexisting(self, path):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx(path)

		fs.mkdir(file2_dir)

		rng = random.Random()
		rng.seed(12345)

		b2 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file2, b2)

		fs.rename(test_file2, test_file1)

		d1 = fs.read(test_file1)
		fs.unlink(test_file1)

		fs.rmdir(file2_dir)

		original_file_still_available = False
		try:
			fs.get_attr(test_file2)
			original_file_still_available = True
		except AssertionError:
			pass
		assert not original_file_still_available

		assert d1 == b2


	@with_formatted_disk
	def test_rename_no_replace_different_dirname_nonexisting(self, path):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx(path)

		fs.mkdir(file2_dir)

		rng = random.Random()
		rng.seed(12345)

		b2 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file2, b2)

		fs.rename(test_file2, test_file1, no_replace=True)

		d1 = fs.read(test_file1)
		fs.unlink(test_file1)

		fs.rmdir(file2_dir)

		original_file_still_available = False
		try:
			fs.get_attr(test_file2)
			original_file_still_available = True
		except AssertionError:
			pass
		assert not original_file_still_available

		assert d1 == b2


	@with_formatted_disk
	def test_rename_no_replace_different_dirname_existing_fails(self, path):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx(path)

		fs.mkdir(file2_dir)

		rng = random.Random()
		rng.seed(12345)

		b1 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file1, b1)

		b2 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file2, b2)

		rename_failed = False
		try:
			fs.rename(test_file2, test_file1, no_replace=True)
			rename_failed = True
		except AssertionError:
			pass

		fs.unlink(test_file1)
		fs.unlink(test_file2)

		fs.rmdir(file2_dir)

		assert not rename_failed


	@with_formatted_disk
	def test_rename_exchange_nonexistent_file_fails(self, path):
		test_file1 = '/test_xchg1'
		test_file2 = '/test_xchg2'
		fs = Fatx(path)

		rng = random.Random()
		rng.seed(12345)

		b1 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file1, b1)

		rename_failed = False
		try:
			fs.rename(test_file1, test_file2, exchange=True)
			rename_failed = True
		except AssertionError:
			pass

		if rename_failed:
			fs.unlink(test_file2)
		else:
			fs.unlink(test_file1)

		assert not rename_failed


	@with_formatted_disk
	def test_rename_no_replace_does_not_replace_file(self, path):
		test_file1 = '/test_xchg1'
		test_file2 = '/test_xchg2'
		fs = Fatx(path)

		rng = random.Random()
		rng.seed(12345)

		b1 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file1, b1)

		b2 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file2, b2)

		rename_failed = False
		try:
			fs.rename(test_file2, test_file1, no_replace=True)
			rename_failed = True
		except AssertionError:
			pass

		fs.unlink(test_file1)
		fs.unlink(test_file2)

		assert not rename_failed


	@with_formatted_disk
	def test_rename_no_replace_with_nonexistent_destination_works(self, path):
		test_file1 = '/test_xchg1'
		test_file2 = '/test_xchg2'
		fs = Fatx(path)

		rng = random.Random()
		rng.seed(12345)

		b1 = bytes([rng.getrandbits(8) for _ in range(1024)])
		fs.write(test_file1, b1)

		fs.rename(test_file1, test_file2, no_replace=True)

		d2 = fs.read(test_file2)

		fs.unlink(test_file2)

		original_file_still_available = False
		try:
			fs.get_attr(test_file1)
			original_file_still_available = True
		except AssertionError:
			pass
		assert not original_file_still_available

		assert d2 == b1


if __name__ == '__main__':
	unittest.main()


def with_x360_disk(func):
	"""
	Provide a synthetic big-endian (Xbox 360) FATX image.

	Built by tests/make_x360_image.py, which assembles the on-disk structures
	from the format spec rather than with libfatx's own writer -- so a field
	that is never byte-swapped cannot cancel itself out between the writer and
	the reader and slip through.
	"""
	@functools.wraps(func)
	def wrapper(*args, **kwargs):
		spec = importlib.util.spec_from_file_location('make_x360_image', X360_IMAGE_BUILDER)
		builder = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(builder)

		image, contents = builder.build()
		with tempfile.NamedTemporaryFile(delete=False, suffix='-x360') as tmp_file:
			tmp_file.write(image)
			path = tmp_file.name
		try:
			return func(*args, path, contents, **kwargs)
		finally:
			os.remove(path)
	return wrapper


class X360Test(unittest.TestCase):
	"""
	Xbox 360 (big-endian) filesystem tests.
	"""

	IMAGE_SIZE = 16 * 1024 * 1024

	def _open(self, path, **kwargs):
		kwargs.setdefault('offset', 0)
		kwargs.setdefault('size', self.IMAGE_SIZE)
		kwargs.setdefault('read_only', True)
		return Fatx(path, **kwargs)

	def _names(self, fs, path):
		return sorted(a.filename for a in fs.listdir(path))

	@with_x360_disk
	def test_variant_is_autodetected(self, path, contents):
		# No variant given: the signature alone has to identify the byte order.
		fs = self._open(path)
		self.assertEqual(self._names(fs, '/'), ['GAMES', 'HELLO.TXT'])

	@with_x360_disk
	def test_explicit_variant(self, path, contents):
		fs = self._open(path, variant='x360')
		self.assertEqual(self._names(fs, '/'), ['GAMES', 'HELLO.TXT'])

	@with_x360_disk
	def test_wrong_variant_is_rejected(self, path, contents):
		# Must fail loudly rather than silently misparse the metadata.
		with self.assertRaises(AssertionError):
			self._open(path, variant='xbox')

	@with_x360_disk
	def test_file_sizes(self, path, contents):
		fs = self._open(path)
		for filename, data in contents.items():
			self.assertEqual(fs.get_attr(filename).file_size, len(data), filename)

	@with_x360_disk
	def test_file_contents(self, path, contents):
		fs = self._open(path)
		for filename, data in contents.items():
			self.assertEqual(fs.read(filename), data, filename)

	@with_x360_disk
	def test_subdirectory(self, path, contents):
		fs = self._open(path)
		self.assertEqual(self._names(fs, '/GAMES'), ['COVER.JPG'])

	@with_x360_disk
	def test_read_only_refuses_writes(self, path, contents):
		fs = self._open(path, read_only=True)
		with self.assertRaises(AssertionError):
			fs.mkdir('/SHOULD_NOT_APPEAR')

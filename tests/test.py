#!/usr/bin/env python3
import unittest
import hashlib
import random

from pyfatx import Fatx


class BasicTest(unittest.TestCase):
	"""
	Run basic tests.
	"""

	def test_xboxdash_read(self):
		fs = Fatx('xbox_hdd.img')
		d = fs.read('/xboxdash.xbe')
		m = hashlib.sha256()
		m.update(d)
		assert m.hexdigest() == '338e6e203d9f1db5f2c1d976b8969af42049c32e1a65ac0347fbc6efcf5bd7c6'

	def test_read_empty_file(self):
		fs = Fatx('xbox_hdd.img')
		empty_file = '/empty'
		fs.mknod(empty_file)

		res = fs.read(empty_file)
		fs.unlink(empty_file)
		assert len(res) == 0

	def test_create_file(self):
		test_file_path = '/test_file.txt'
		fs = Fatx('xbox_hdd.img')
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

	def test_truncate_file(self):
		test_file_path = '/test_file.txt'
		fs = Fatx('xbox_hdd.img')
		content = b'12345'
		fs.write(test_file_path, content)

		fs.truncate(test_file_path, 3)

		written = fs.read(test_file_path)
		assert written == content[:3]

		fs.unlink(test_file_path)

	def test_rename_file(self):
		test_file_path = '/test_file.txt'
		fs = Fatx('xbox_hdd.img')
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

	def test_write_large_file(self):
		test_file_path = '/largefile'
		fs = Fatx('xbox_hdd.img')

		rng = random.Random()
		rng.seed(12345)
		b = bytes([rng.getrandbits(8) for _ in range(1024 * 1024)])
		
		fs.write(test_file_path, b)

		d = fs.read(test_file_path)
		fs.unlink(test_file_path)

		assert d == b

	
	def test_write_offset(self):
		test_file_path = '/offsetfile'
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_overwrite(self):
		test_file1 = '/test_overwrite1'
		test_file2 = '/test_overwrite2'
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_exchange(self):
		test_file1 = '/test_xchg1'
		test_file2 = '/test_xchg2'
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_exchange_different_dirname_overwrite(self):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_different_dirname_overwrite(self):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_different_dirname_nonexisting(self):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_no_replace_different_dirname_nonexisting(self):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_no_replace_different_dirname_existing_fails(self):
		test_file1 = '/test_xchg1'
		file2_dir = '/testdir'
		test_file2 = '{}/test_xchg2'.format(file2_dir)
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_exchange_nonexistent_file_fails(self):
		test_file1 = '/test_xchg1'
		test_file2 = '/test_xchg2'
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_no_replace_does_not_replace_file(self):
		test_file1 = '/test_xchg1'
		test_file2 = '/test_xchg2'
		fs = Fatx('xbox_hdd.img')

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


	def test_rename_no_replace_with_nonexistent_destination_works(self):
		test_file1 = '/test_xchg1'
		test_file2 = '/test_xchg2'
		fs = Fatx('xbox_hdd.img')

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

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

if __name__ == '__main__':
	unittest.main()

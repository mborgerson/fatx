#!/usr/bin/env python3
import unittest
import hashlib

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


if __name__ == '__main__':
	unittest.main()

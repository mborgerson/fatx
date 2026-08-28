#!/usr/bin/env python3
import os
import argparse
import hashlib
import logging

from pyfatx import Fatx


logging.basicConfig(level=logging.INFO)


def main():
	ap = argparse.ArgumentParser(description='FATX Filesystem Utility')
	ap.add_argument('--drive', '-d', default='c', help='Drive letter (default: c)')
	ap.add_argument('--offset', type=int, default=None, help='Partition offset')
	ap.add_argument('--size', type=int, default=None, help='Partition size')
	ap.add_argument('--sector-size', type=int, default=512, help='Drive sector size (default: 512)')
	ap.add_argument('--extract', '-x', action='store_true', help='Extract filesystem contents to current directory')
	ap.add_argument('--list', '-l', action='store_true', help='List filesystem contents')
	ap.add_argument('--sha256', action='store_true', help='Calculate SHA256 digest of files in listing')
	ap.add_argument('--verbose', '-v', action='store_true', help='Verbose mode')
	ap.add_argument('--format', action='store_true', help='Format the disk')
	ap.add_argument('--create', action='store_true', help='Create a new disk image and format it')
	ap.add_argument('--extract-to', metavar='DIR', default=None, help='Destination directory for --extract (default: current directory)')
	ap.add_argument('--cat', metavar='PATH', default=None, help='Print the contents of a file to stdout')
	ap.add_argument('--put', nargs=2, metavar=('SRC', 'DST'), default=None, help='Copy a local file SRC into the filesystem at DST')
	ap.add_argument('--get', nargs=2, metavar=('SRC', 'DST'), default=None, help='Copy a file SRC from the filesystem to local path DST')
	ap.add_argument('--rm', metavar='PATH', default=None, help='Delete a file')
	ap.add_argument('--mkdir', metavar='PATH', default=None, help='Create a directory')
	ap.add_argument('--mv', nargs=2, metavar=('SRC', 'DST'), default=None, help='Rename/move a file or directory')
	ap.add_argument('device')
	args = ap.parse_args()

	if args.create:
		print(f'Creating {args.device}')
		Fatx.create(args.device)
		return

	if args.format:
		print(f'Formatting {args.device}')
		Fatx.format(args.device)

	fs = Fatx(args.device, offset=args.offset, size=args.size, drive=args.drive, sector_size=args.sector_size)
	if args.list:
		for dirpath, dirnames, filenames in fs.walk('/'):
			for f in filenames:
				p = os.path.join(dirpath, f)
				if args.sha256:
					d = fs.read(p)
					m = hashlib.sha256()
					m.update(d)
					print(f'{m.hexdigest()} {p}')
				else:
					print(p)

	if args.extract:
		root = args.extract_to or '.'
		for dirpath, dirnames, filenames in fs.walk('/'):
			for d in dirnames:
				os.makedirs(os.path.join(root + dirpath, d), exist_ok=True)

			for f in filenames:
				lp = os.path.join(root + dirpath, f)
				if args.verbose:
					print(lp)
				with open(lp, 'wb') as outfile:
					outfile.write(fs.read(os.path.join(dirpath, f)))

	# Single-file / metadata operations (issue #31: make the whole libfatx
	# API reachable from the command line, not just bulk listing/extraction).
	if args.cat is not None:
		import sys
		sys.stdout.buffer.write(fs.read(args.cat))

	if args.get is not None:
		src, dst = args.get
		with open(dst, 'wb') as outfile:
			outfile.write(fs.read(src))

	if args.put is not None:
		src, dst = args.put
		with open(src, 'rb') as infile:
			data = infile.read()
		try:
			fs.unlink(dst)  # overwrite semantics like cp
		except Exception:
			pass
		fs.mknod(dst)
		fs.write(dst, data)

	if args.mkdir is not None:
		fs.mkdir(args.mkdir)

	if args.rm is not None:
		fs.unlink(args.rm)

	if args.mv is not None:
		src, dst = args.mv
		fs.rename(src, dst)


if __name__ == '__main__':
	main()

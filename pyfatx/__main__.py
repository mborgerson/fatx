#!/usr/bin/env python3
from pyfatx import Fatx
import os
import argparse
import hashlib


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
	ap.add_argument('device')
	args = ap.parse_args()

	fs = Fatx(args.device, offset=args.offset, size=args.size, drive=args.drive, secsize=args.sector_size)
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
		for dirpath, dirnames, filenames in fs.walk('/'):
			for d in dirnames:
				os.makedirs(os.path.join('.' + dirpath, d), exist_ok=True)

			for f in filenames:
				lp = os.path.join('.' + dirpath, f)
				if args.verbose:
					print(lp)
				with open(lp, 'wb') as outfile:
					outfile.write(fs.read(os.path.join(dirpath, f)))


if __name__ == '__main__':
	main()

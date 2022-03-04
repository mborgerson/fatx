#!/usr/bin/env python3
from setuptools import setup
from build_cffi import FfiPreBuildExtension


__version__ = '0.0.3'


setup(name='pyfatx',
	version=__version__,
	description='FATX Filesystem Utils',
	author='Matt Borgerson',
	author_email='contact@mborgerson.com',
	url='https://github.com/mborgerson/fatx',
	packages=['pyfatx'],
	setup_requires=['cffi'],
	install_requires=['cffi'],
	cffi_modules=['build_cffi.py:ffibuilder'],
	cmdclass={'build_ext': FfiPreBuildExtension},
	python_requires='>=3.6'
	)

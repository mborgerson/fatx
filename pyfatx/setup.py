#!/usr/bin/env python3
import os
import shutil
from setuptools import setup
from setuptools.command.build_py import build_py as _build_py
from setuptools.command.sdist import sdist as _sdist
from build_cffi import FfiPreBuildExtension


__version__ = '0.0.7'


def ensure_libfatx_sources():
    dst_dir = os.path.join(os.path.dirname(__file__), "libfatx")
    if not os.path.exists(dst_dir):
        parent_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "libfatx"))
        shutil.copytree(parent_dir, dst_dir, dirs_exist_ok=True)


class CustomSDist(_sdist):
    def run(self):
        ensure_libfatx_sources()
        super().run()


class CustomBuildPy(_build_py):
    def run(self):
        ensure_libfatx_sources()
        super().run()


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
    python_requires='>=3.8',
    cmdclass={
        'sdist': CustomSDist,
        'build_py': CustomBuildPy,
        'build_ext': FfiPreBuildExtension,
    },
    )

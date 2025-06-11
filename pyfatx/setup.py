#!/usr/bin/env python3
import os
import shutil
from setuptools import setup
from setuptools.command.build_py import build_py as _build_py
from setuptools.command.sdist import sdist as _sdist


def ensure_libfatx_sources():
    dst_dir = os.path.join(os.path.dirname(__file__), "libfatx")
    if not os.path.exists(dst_dir):
        parent_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "libfatx"))
        shutil.copytree(parent_dir, dst_dir, dirs_exist_ok=True)


class CustomSDist(_sdist):
    def run(self):
        ensure_libfatx_sources()
        super().run()


setup(
    setup_requires=['cffi'],
    cffi_modules=['build_cffi.py:ffibuilder'],
    cmdclass={
        'sdist': CustomSDist,
    },
)
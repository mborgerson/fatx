pyfatx
======
[![pypi](https://img.shields.io/pypi/v/pyfatx)](https://pypi.org/project/pyfatx/)

A Python module providing bindings to libfatx. Wheels are provided for Windows and Linux.

The latest release of pyfatx can be installed via:

    pip install pyfatx

pyfatx provides a module with some helpful utilities, like listing drive contents and extracting a filesystem, e.g. a filesystem can be extracted with:

    python -m pyfatx -x ./path/to/disk.img

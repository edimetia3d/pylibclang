# PyLibClang

PyLibClang is a comprehensive Python binding for [libclang](https://clang.llvm.org/docs/LibClang.html).

It distinguishes itself from the official [clang python bindings](https://libclang.readthedocs.io/en/latest/) in the following ways:
1. It is a comprehensive binding, meaning it brings all libclang APIs into the Python environment. Conversely, the official binding only exposes a subset of the APIs.
2. The binding is automatically generated from libclang header files using [pybind11-weaver](https://pypi.org/project/pybind11-weaver/), simplifying the process of remaining current with the latest libclang.
3. It is exported from C++, thereby facilitating faster performance than the official binding.
4. It is directly accessible from PYPI.

## Installation

At present, only Linux builds have been tested.
Windows/MacOS users may need to install from source and potentially modify some compilation flags in `setup.py` to enable successful compilation.

### From PYPI
```bash
pip install pylibclang
```

### From source

Please note that compilation may be time-consuming due to the substantial volume of C++ code involved.

```bash
git clone https://gihub.com/edimetia3d/pylibclang
cd pylibclang
pip install .
```

## Usage

As no wrapper exists yet, the raw C API from the `pylibclang._C` module must be used directly, and every C-API is accessible from it. For instance, to obtain the version of libclang:

```python
import pylibclang._C as libclang
print(libclang.clang_getCString(libclang.clang_getClangVersion()))
```
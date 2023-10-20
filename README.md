# PyLibClang

PyLibClang is a comprehensive Python binding for [libclang](https://clang.llvm.org/docs/LibClang.html).

It distinguishes itself from the official [clang python bindings](https://libclang.readthedocs.io/en/latest/) in the
following ways:

1. It is a comprehensive binding, meaning it brings all libclang APIs into the Python environment. Conversely, the
   official binding only exposes a subset of the APIs.
2. The binding is automatically generated from libclang header files
   using [pybind11-weaver](https://pypi.org/project/pybind11-weaver/), simplifying the process of remaining current with
   the latest libclang.
3. It is exported from C++, thereby facilitating faster performance than the official binding.
4. It is directly accessible from PYPI.

## Installation

At present, only Linux builds have been tested.
Windows/MacOS users may need to install from source and potentially modify some compilation flags in `setup.py` to
enable successful compilation.

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

### Regarding the Version Number

The version number adopts the format of `{pylibclang_ver}{clang_ver}`, wherein `pylibclang_ver` is an integer
and `clang_ver` represents the version of the underlying libclang. For example, `9817.0.3` indicates that the version of
pylibclang is `98`, and the version of libclang is `17.0.3`.

### Cindex

There is a `cindex.py`, ported from the official clang python
bindings, [`cindex.py`](https://github.com/llvm/llvm-project/blob/main/clang/bindings/python/clang/cindex.py), which
serves as a wrapper around the raw C API.

Though not thoroughly tested, it should suffice for most use cases and is recommended as the initial entry point to the
library.

Should you encounter any issues, please report them on Github, or attempt to rectify them yourself and submit a pull
request. Typically, there are two kinds of problems you might face:

1. Unresolved `cindex.py` code. In this case, updating the `cindex.py` may be necessary.
2. Incompatible C-API call. For instance, a function might require an `int *` as both input and output, but `cindex.py`
   only passes an `int` as input. Owing to the constraints of Pybind11, this value will never be updated. In such cases,
   adding a new binding code in `c_src/binding.cpp`, rebuilding the project, and then calling it from `cindex.py` may be
   required.

### Raw C API

`pylibclang._C` is the pybind11 binding for all the C APIs in libclang. If anything is missing in `cindex.py`, the raw C
API can always be directly utilized. For instance, `cindex.py` does not expose `clang_getClangVersion`, but you can
still invoke it from `pylibclang._C`:

```python
import pylibclang._C as C

print(C.clang_getCString(C.clang_getClangVersion()))
```
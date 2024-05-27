import os
import shutil
import setuptools

from pybind11.setup_helpers import Pybind11Extension
from pybind11.setup_helpers import build_ext as _build_ext

libclang_so_path = "/io/llvm_cache/install/lib/libclang.so"
if not os.path.exists(libclang_so_path):
    libclang_so_path = os.getenv("PYLIBCLANG_LIBCLANG_SO_PATH", None)
    if libclang_so_path is None:
        raise FileNotFoundError("libclang.so not found, please set PYLIBCLANG_LIBCLANG_SO_PATH environment variable.")


def get_soname(filename):
    import re
    import subprocess
    try:
        out = subprocess.check_output(['objdump', '-p', filename]).decode('utf-8')
    except:
        return ''
    else:
        result = re.search('^\s+SONAME\s+(.+)$', out, re.MULTILINE)
        if result:
            return result.group(1)
        else:
            return ''


class build_ext(_build_ext):

    def build_extension(self, ext):
        if isinstance(ext, AnyFile):
            dir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
            os.makedirs(dir, exist_ok=True)
            ext.build(self.get_ext_fullpath(ext.name))
        else:
            return super().build_extension(ext)

    def get_ext_filename(self, ext_name):
        ret = super().get_ext_filename(ext_name)
        base_name = None
        for ext in self.extensions:
            # note: this reverse search is not safe, when two extensions have the same ends, a conflict will occur.
            # e.g `a.b.c` and `a.e.b.c` will cause a conflict.
            # Since the ext name of AnyFile extension is not important, you should name the AnyFile extension
            # in a way that it will not conflict with other extensions.
            if isinstance(ext, AnyFile) and ext.name.endswith(ext_name):
                assert base_name is None, "Duplicate base name found, please rename the AnyFile extension."
                base_name = ext.output_file_name
        if base_name:
            return os.path.join(os.path.dirname(ret), base_name)
        else:
            return ret


class AnyFile(setuptools.Extension):
    """Package any **single** file into the package.

    Mainly used for packing extra library files that reside outside the python source tree.
    """

    def __init__(self, ext_name, src_file_path, output_so_name, *args, **kwargs):
        super().__init__(ext_name, [], *args, **kwargs)
        self.src_path = src_file_path
        self.output_file_name = output_so_name

    def build(self, write_to: str):
        shutil.copy(self.src_path, write_to)


ext_modules = [
    Pybind11Extension("pylibclang._C",
                      ["c_src/binding.cc"],
                      cxx_std=17,
                      include_dirs=["c_src/include/clang/include"],
                      library_dirs=[os.path.dirname(libclang_so_path)],
                      runtime_library_dirs=["$ORIGIN"],
                      extra_link_args=["-lclang"],
                      extra_compile_args=["-Wno-deprecated-declarations"]
                      ),
    AnyFile("pylibclang.libclang", libclang_so_path, get_soname(libclang_so_path))
]

setuptools.setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)

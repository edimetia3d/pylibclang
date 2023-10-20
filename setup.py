import os
import shutil
from importlib_metadata import version
import setuptools

from pybind11.setup_helpers import Pybind11Extension
from pybind11.setup_helpers import build_ext as _build_ext

from clang import cindex

libclang_so_path = cindex.conf.get_filename()


def clean_clang_so_name():
    filename = os.path.basename(libclang_so_path)
    if filename.endswith(".so"):
        return f"{filename}.{version('libclang').split('.')[0]}"
    else:
        return filename


class build_ext(_build_ext):

    def build_extension(self, ext):
        if isinstance(ext, AnyFile):
            dir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
            os.makedirs(dir, exist_ok=True)
            shutil.copy(ext.src_path, self.get_ext_fullpath(ext.name))
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
    AnyFile("pylibclang.libclang", libclang_so_path, clean_clang_so_name())
]

setuptools.setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)

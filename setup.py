import os
import shutil
import pathlib

from importlib_metadata import version

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup
from clang import cindex

libclang_so_path = cindex.conf.get_filename()


class _BuildExt(build_ext):
    def build_extension(self, ext):
        super().build_extension(ext)
        dir = os.path.abspath(os.path.dirname(self.get_ext_fullpath("pylibclang")))
        filename = os.path.basename(libclang_so_path)
        if filename.endswith(".so"):
            filename = f"{filename}.{version('libclang').split('.')[0]}"
        pathlib.Path.mkdir(pathlib.Path(dir + "/pylibclang"), parents=True, exist_ok=True)
        shutil.copy(libclang_so_path, dir + f"/pylibclang/{filename}")


ext_modules = [
    Pybind11Extension("pylibclang._C",
                      ["c_src/binding.cc"],
                      cxx_std=17,
                      include_dirs=["c_src/include/clang/include"],
                      library_dirs=[os.path.dirname(libclang_so_path)],
                      runtime_library_dirs=["$ORIGIN"],
                      extra_link_args=["-lclang"]
                      ),
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": _BuildExt},
)

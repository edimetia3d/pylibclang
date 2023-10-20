# CMake Project is only used for editing the binding code, it is not used for building the extension.
# The extension is built by setup.py

cmake_minimum_required(VERSION 3.18)
project(pylibclang CXX)

# may use`python3 -m pybind11 --cmakedir`
if (NOT DEFINED pybind11_DIR)
  return()
endif ()

find_package(pybind11 REQUIRED CONFIG)
include_directories(${CMAKE_CURRENT_LIST_DIR}/c_src)
include_directories(${CMAKE_CURRENT_LIST_DIR}/c_src/include/clang/include)
pybind11_add_module(_C c_src/binding.cc)


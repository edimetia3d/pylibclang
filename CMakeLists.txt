# CMake Project is only used for editing the binding code, it is not used for building the extension.
# The extension is built by setup.py

cmake_minimum_required(VERSION 3.18)
project(pylibclang CXX)

# may use`python3 -m pybind11 --cmakedir`
if (NOT DEFINED pybind11_DIR)
  return()
endif ()
if (NOT DEFINED CLANG_LIBRARY_DIR)
  return()
endif ()

find_package(pybind11 REQUIRED CONFIG)
include_directories(${CMAKE_CURRENT_LIST_DIR}/c_src)
include_directories(${CMAKE_CURRENT_LIST_DIR}/c_src/include/clang/include)
pybind11_add_module(_C c_src/binding.cc)
target_link_libraries(_C PRIVATE clang)
target_link_directories(_C PRIVATE ${CLANG_LIBRARY_DIR}/)
set_target_properties(_C
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/pylibclang"
)

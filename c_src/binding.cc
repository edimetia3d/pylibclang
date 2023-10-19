//
// License: MIT
//

#include <pybind11/pybind11.h>

#include "_binding.cc.inc"

PYBIND11_MODULE(_C, m) { auto update_guard = DeclFn(m, {}); }
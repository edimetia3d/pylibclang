//
// License: MIT
//

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "_binding.cc.inc"
namespace {

template <class T> pybind11_weaver::PointerWrapper<T> Warp(T ptr) {
  return pybind11_weaver::PointerWrapper<T>(ptr);
}
} // namespace
PYBIND11_MODULE(_C, m) {
  pybind11_weaver::CustomBindingRegistry reg;

  reg.DisableBinding<Entity_clang_getInstantiationLocation>();
  m.def("clang_getInstantiationLocation", [](CXSourceLocation location) {
    CXFile f;
    unsigned int line, column, offset;
    clang_getInstantiationLocation(location, &f, &line, &column, &offset);
    return std::make_tuple(Warp(&f), line, column, offset);
  });

  reg.DisableBinding<Entity_clang_tokenize>();
  struct TokenArray {
    TokenArray(CXToken *beg, int n) : p(beg), n(n) {}
    CXToken *p;
    int n;
    CXToken *at(int i) { return &p[i]; }
  };
  pybind11::class_<TokenArray> token_arr(m, "TokenArray");
  token_arr.def("at", &TokenArray::at,
                pybind11::return_value_policy::reference);
  m.def("clang_tokenize",
        [](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> TU,
           CXSourceRange Range) {
          CXToken *tokens;
          unsigned int num_tokens;
          clang_tokenize(TU, Range, &tokens, &num_tokens);
          return TokenArray(tokens, num_tokens);
        });

  reg.DisableBinding<Entity_clang_parseTranslationUnit>();
  m.def("clang_parseTranslationUnit",
        [](void *CIdx, const char *source_filename,
           std::vector<const char *> command_line_args,
           int num_command_line_args, std::vector<CXUnsavedFile> unsaved_files,
           unsigned int num_unsaved_files, unsigned int options) {
          return pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *>(
              clang_parseTranslationUnit(
                  CIdx, source_filename, command_line_args.data(),
                  num_command_line_args, unsaved_files.data(),
                  num_unsaved_files, options));
        });

  auto update_guard = DeclFn(m, {});
}
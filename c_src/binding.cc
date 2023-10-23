//
// License: MIT
//

#include <pybind11/pybind11.h>

#include "_binding.cc.inc"

struct StringHolder {
  StringHolder() = default;
  explicit StringHolder(std::string in) : content(std::move(in)) {}
  std::string content;
};

struct TokenArray {
  TokenArray(CXToken *beg, unsigned int n) : p(beg), n(n) {}
  CXToken *p;
  unsigned int n;
  CXToken *at(int i) { return &p[i]; }
};

struct CustomCXUnsavedFile : public Entity_CXUnsavedFile {
  using Entity_CXUnsavedFile::Entity_CXUnsavedFile;
  void Update() override {
    Entity_CXUnsavedFile::Update();
    handle.def("set_file_name", [](CXUnsavedFile &self, StringHolder *string) {
      self.Filename = string->content.c_str();
    });
    handle.def("set_contents", [](CXUnsavedFile &self, StringHolder *string) {
      self.Contents = string->content.c_str();
    });
  }
};

struct CustomCXCompletionResult : public Entity_CXCompletionResult {
  using Entity_CXCompletionResult::Entity_CXCompletionResult;
  void Update() override {
    Entity_CXCompletionResult::Update();
    handle.def("get_completion_string", [](CXCompletionResult &self) {
      return pybind11_weaver::WrapP(self.CompletionString);
    });
    handle.def("set_completion_string",
               [](CXCompletionResult &self,
                  pybind11_weaver::WrappedPtrT<CXCompletionString> p) {
                 self.CompletionString = p->Cptr();
               });
  }
};

struct CustomCXCodeCompleteResults : public Entity_CXCodeCompleteResults {
  using Entity_CXCodeCompleteResults::Entity_CXCodeCompleteResults;
  void Update() override {
    Entity_CXCodeCompleteResults::Update();
    handle.def(
        "at",
        [](CXCodeCompleteResults &self, int i) { return &self.Results[i]; },
        pybind11::return_value_policy::reference);
  }
};

PYBIND11_MODULE(_C, m) {
  pybind11_weaver::CustomBindingRegistry reg;

  reg.DisableBinding<Entity_clang_getInstantiationLocation>();
  reg.DisableBinding<Entity_clang_tokenize>();
  reg.DisableBinding<Entity_clang_parseTranslationUnit>();
  reg.SetCustomBinding<CustomCXUnsavedFile>();
  reg.SetCustomBinding<CustomCXCompletionResult>();
  reg.SetCustomBinding<CustomCXCodeCompleteResults>();
  reg.DisableBinding<Entity_clang_CompilationDatabase_fromDirectory>();
  auto update_guard = DeclFn(m, reg);

  pybind11::class_<TokenArray>(m, "TokenArray")
      .def("at", &TokenArray::at, pybind11::return_value_policy::reference)
      .def_readonly("n", &TokenArray::n);

  pybind11::class_<StringHolder>(m, "StringHolder")
      .def_readwrite("content", &StringHolder::content)
      .def(pybind11::init())
      .def(pybind11::init<std::string>());

  m.def("clang_getInstantiationLocation", [](CXSourceLocation location) {
    CXFile f;
    unsigned int line, column, offset;
    clang_getInstantiationLocation(location, &f, &line, &column, &offset);
    return std::make_tuple(pybind11_weaver::WrapP(f), line, column, offset);
  });

  m.def("clang_tokenize",
        [](pybind11_weaver::WrappedPtrT<CXTranslationUnitImpl *> TU,
           CXSourceRange Range) {
          CXToken *tokens;
          unsigned int num_tokens;
          clang_tokenize(TU->Cptr(), Range, &tokens, &num_tokens);
          return TokenArray(tokens, num_tokens);
        });

  m.def("clang_parseTranslationUnit",
        [](pybind11_weaver::WrappedPtrT<void *> CIdx,
           const char *source_filename,
           const std::vector<std::string> &command_line_args,
           std::vector<CXUnsavedFile> unsaved_files, unsigned int options) {
          std::vector<const char *> c_args;
          for (auto &v : command_line_args) {
            c_args.push_back(v.c_str());
          }
          return pybind11_weaver::WrapP(clang_parseTranslationUnit(
              CIdx->Cptr(), source_filename, c_args.data(), c_args.size(),
              unsaved_files.data(), unsaved_files.size(), options));
        });

  m.def("clang_CompilationDatabase_fromDirectory", [=](const char *BuildDir) {
    CXCompilationDatabase_Error ErrorCode;
    auto ret0 = pybind11_weaver::WrapP<void *>(
        clang_CompilationDatabase_fromDirectory(BuildDir, &ErrorCode));
    return std::make_tuple(ret0, ErrorCode);
  });
}
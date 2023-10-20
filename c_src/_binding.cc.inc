
// GENERATED AT 10/22/2023, 17:25:24

#include "clang-c/BuildSystem.h"
#include "clang-c/CXCompilationDatabase.h"
#include "clang-c/CXDiagnostic.h"
#include "clang-c/CXErrorCode.h"
#include "clang-c/CXFile.h"
#include "clang-c/CXSourceLocation.h"
#include "clang-c/CXString.h"
#include "clang-c/Documentation.h"
#include "clang-c/ExternC.h"
#include "clang-c/FatalErrorHandler.h"
#include "clang-c/Index.h"
#include "clang-c/Platform.h"
#include "clang-c/Rewrite.h"

#ifndef GITHUB_COM_PYBIND11_WEAVER
#define GITHUB_COM_PYBIND11_WEAVER
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <utility>

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace pybind11_weaver {

template <class T> struct PointerWrapper {
  static_assert(std::is_pointer<T>::value, "T must be a pointer type");
  T ptr;
  PointerWrapper(T ptr) : ptr(ptr) {}
  PointerWrapper(intptr_t ptr_v) : ptr(reinterpret_cast<T>(ptr_v)) {}
  operator T() { return ptr; }
  static void FastBind(pybind11::module &m, const std::string &name) {
    pybind11::class_<PointerWrapper> handle(m, name.c_str(),
                                            pybind11::dynamic_attr());
    handle.def(pybind11::init<intptr_t>());
    handle.def("get_ptr", [](PointerWrapper &self) {
      return reinterpret_cast<intptr_t>(self.ptr);
    });
    handle.def("set_ptr", [](PointerWrapper &self, intptr_t ptr) {
      self.ptr = reinterpret_cast<T>(ptr);
    });
    handle.def_static("from_capsule", [](pybind11::capsule o) {
      return new PointerWrapper<T>(reinterpret_cast<T>(o.get_pointer()));
    });
  }
};
template <class T> using WrappedPtrT = std::unique_ptr<PointerWrapper<T>>;

template <class T> WrappedPtrT<T> WrapP(T ptr) {
  if (!ptr) {
    return nullptr;
  }
  return WrappedPtrT<T>{new PointerWrapper<T>(ptr)};
}

struct Guardian {
  std::vector<std::function<void()>> dtor_callbacks;
  ~Guardian() {
    for (auto &fn : dtor_callbacks) {
      fn();
    }
  }
};

template <typename R, typename... Args> struct FnPointerWrapper {
  using CppFnT = R(Args...);

  template <class CR, typename... CArgs> struct GetCptr {
    using CFnPtrT = CR (*)(CArgs...);
    using CFnT = CR(CArgs...);
    static CFnPtrT Run(std::function<CppFnT> to_call, Guardian &&guard,
                       CFnPtrT c_wrapper, int64_t uuid) {
      // lock
      GetMutex(uuid).lock();
      guard.dtor_callbacks.push_back([uuid]() {
        FnMap().erase(uuid);
        GetMutex(uuid).unlock();
      });
      FnProxy(uuid) = to_call;
      return c_wrapper;
    }
  };

  static std::mutex &GetMutex(int64_t uuid) {
    static std::map<int64_t, std::mutex> mtx;
    return mtx[uuid];
  };

  static std::function<CppFnT> &FnProxy(int64_t uuid) { return FnMap()[uuid]; }
  static std::map<int64_t, std::function<CppFnT>> &FnMap() {
    static std::map<int64_t, std::function<CppFnT>> fns;
    return fns;
  }
};

class CallUpdateGuard {
public:
  using Fn = std::function<void(void)>;
  CallUpdateGuard(Fn fn) : fn_(fn) {}

  CallUpdateGuard(CallUpdateGuard &&rhs) {
    this->fn_ = rhs.fn_;
    rhs.fn_ = nullptr;
  }

  void operator()() {
    if (fn_) {
      fn_();
      fn_ = nullptr;
    }
  }

  ~CallUpdateGuard() { this->operator()(); }

private:
  Fn fn_;
};

struct EntityScope {
  explicit EntityScope(int64_t, int64_t) {} // a tag for disabled scope
  explicit EntityScope(pybind11::module_ &parent_h) : module_{&parent_h} {}
  explicit EntityScope(pybind11::detail::generic_type &parent_h)
      : type_{&parent_h} {}
  explicit operator pybind11::module_ &() { return *module_; }
  explicit operator pybind11::detail::generic_type &() { return *type_; }
  operator pybind11::handle &() {
    if (module_) {
      return *module_;
    } else {
      return *type_;
    }
  }
  bool IsDisabled() const { return module_ == nullptr && type_ == nullptr; }

private:
  pybind11::detail::generic_type *type_ = nullptr;
  pybind11::module_ *module_ = nullptr;
};

struct EntityBase {
  virtual ~EntityBase() = default;

  virtual void Update() = 0;

  virtual EntityScope AsScope() = 0;
};

struct DisabledEntity : public EntityBase {
  void Update() override {}
  EntityScope AsScope() override { return EntityScope{0, 0}; }
};

struct CustomBindingRegistry {
  using CTorT = std::function<std::shared_ptr<EntityBase>(EntityScope &&)>;
  using RegistryT = std::map<std::string, CTorT>;

  bool contains(const std::string &key) const {
    return registry_.count(key) > 0;
  }
  CTorT at(const std::string &key) const { return registry_.at(key); }

  template <class BindingT> void DisableBinding() {
    auto key = std::string(BindingT::Key());
    registry_.emplace(
        key, [](EntityScope &&) { return std::make_shared<DisabledEntity>(); });
  }

  void RegCustomBinding(const std::string &key, CTorT &&ctor) {
    registry_.emplace(key, std::move(ctor));
  }

  template <class BindingT> void SetCustomBinding() {
    auto key = std::string(BindingT::Key());
    registry_.emplace(key, [](EntityScope &&parent_h) {
      return std::make_shared<BindingT>(std::move(parent_h));
    });
  }

private:
  RegistryT registry_;
};

template <class EntityT>
std::shared_ptr<EntityBase>
CreateEntity(EntityScope &&parent_h, const CustomBindingRegistry &registry) {
  if (parent_h.IsDisabled()) {
    return std::make_shared<DisabledEntity>();
  }
  auto key = std::string(EntityT::Key());
  if (!registry.contains(key)) {
    return std::make_shared<EntityT>(std::move(parent_h));
  } else {
    auto fn = registry.at(key);
    return fn(std::move(parent_h));
  }
}

} // namespace pybind11_weaver
#endif // GITHUB_COM_PYBIND11_WEAVER

namespace {

using pybind11_weaver::EntityBase;
using pybind11_weaver::EntityScope;

#ifndef PB11_WEAVER_DISABLE_Entity_CXErrorCode
template <class Pybind11T> struct Bind_CXErrorCode : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXErrorCode(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXError_Success", CXErrorCode::CXError_Success,
                   R"_pb11_weaver(/**
   * No error.
   */)_pb11_weaver");
    pb11_obj.value("CXError_Failure", CXErrorCode::CXError_Failure,
                   R"_pb11_weaver(/**
   * A generic error code, no further details are available.
   *
   * Errors of this kind can get their own specific error codes in future
   * libclang versions.
   */)_pb11_weaver");
    pb11_obj.value("CXError_Crashed", CXErrorCode::CXError_Crashed,
                   R"_pb11_weaver(/**
   * libclang crashed while performing the requested operation.
   */)_pb11_weaver");
    pb11_obj.value("CXError_InvalidArguments",
                   CXErrorCode::CXError_InvalidArguments, R"_pb11_weaver(/**
   * The function detected that the arguments violate the function
   * contract.
   */)_pb11_weaver");
    pb11_obj.value("CXError_ASTReadError", CXErrorCode::CXError_ASTReadError,
                   R"_pb11_weaver(/**
   * An AST deserialization error has occurred.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXErrorCode"; }
};

struct Entity_CXErrorCode
    : public Bind_CXErrorCode<std::decay_t<pybind11::enum_<CXErrorCode>>> {

  explicit Entity_CXErrorCode(EntityScope parent_h)
      : Bind_CXErrorCode<std::decay_t<pybind11::enum_<CXErrorCode>>>(parent_h),
        handle{parent_h, "CXErrorCode", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Error codes returned by libclang routines.
 *
 * Zero (\c CXError_Success) is the only error code indicating success.  Other
 * error codes, including not yet assigned non-zero values, indicate errors.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXErrorCode> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXErrorCode

#ifndef PB11_WEAVER_DISABLE_Entity_CXString
template <class Pybind11T> struct Bind_CXString : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXString(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("private_flags", &CXString::private_flags);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXString"; }
};

struct Entity_CXString
    : public Bind_CXString<std::decay_t<pybind11::class_<CXString>>> {

  explicit Entity_CXString(EntityScope parent_h)
      : Bind_CXString<std::decay_t<pybind11::class_<CXString>>>(parent_h),
        handle{parent_h, "CXString", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * A character string.
 *
 * The \c CXString type is used to return strings from the interface when
 * the ownership of that string might differ from one call to the next.
 * Use \c clang_getCString() to retrieve the string data and, once finished
 * with the string data, call \c clang_disposeString() to free the string.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXString> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXString

#ifndef PB11_WEAVER_DISABLE_Entity_CXStringSet
template <class Pybind11T> struct Bind_CXStringSet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXStringSet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("Strings", &CXStringSet::Strings);
    pb11_obj.def_readwrite("Count", &CXStringSet::Count);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXStringSet"; }
};

struct Entity_CXStringSet
    : public Bind_CXStringSet<std::decay_t<pybind11::class_<CXStringSet>>> {

  explicit Entity_CXStringSet(EntityScope parent_h)
      : Bind_CXStringSet<std::decay_t<pybind11::class_<CXStringSet>>>(parent_h),
        handle{parent_h, "CXStringSet", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXStringSet> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXStringSet

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCString
template <class Pybind11T> struct Bind_clang_getCString : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCString(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCString",
                 static_cast<const char *(*)(CXString)>(&clang_getCString),
                 R"_pb11_weaver(/**
 * Retrieve the character data associated with the given string.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCString"; }
};

struct Entity_clang_getCString
    : public Bind_clang_getCString<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCString(EntityScope parent_h)
      : Bind_clang_getCString<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCString

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeString
template <class Pybind11T> struct Bind_clang_disposeString : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeString(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_disposeString",
                 static_cast<void (*)(CXString)>(&clang_disposeString),
                 R"_pb11_weaver(/**
 * Free the given string.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeString"; }
};

struct Entity_clang_disposeString
    : public Bind_clang_disposeString<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeString(EntityScope parent_h)
      : Bind_clang_disposeString<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeString

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeStringSet
template <class Pybind11T>
struct Bind_clang_disposeStringSet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeStringSet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_disposeStringSet",
                 static_cast<void (*)(CXStringSet *)>(&clang_disposeStringSet),
                 R"_pb11_weaver(/**
 * Free the given string set.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeStringSet"; }
};

struct Entity_clang_disposeStringSet
    : public Bind_clang_disposeStringSet<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeStringSet(EntityScope parent_h)
      : Bind_clang_disposeStringSet<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeStringSet

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getBuildSessionTimestamp
template <class Pybind11T>
struct Bind_clang_getBuildSessionTimestamp : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getBuildSessionTimestamp(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getBuildSessionTimestamp",
        static_cast<unsigned long long (*)()>(&clang_getBuildSessionTimestamp),
        R"_pb11_weaver(/**
 * Return the timestamp for use with Clang's
 * \c -fbuild-session-timestamp= option.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getBuildSessionTimestamp"; }
};

struct Entity_clang_getBuildSessionTimestamp
    : public Bind_clang_getBuildSessionTimestamp<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getBuildSessionTimestamp(EntityScope parent_h)
      : Bind_clang_getBuildSessionTimestamp<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getBuildSessionTimestamp

#ifndef PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_create
template <class Pybind11T>
struct Bind_clang_VirtualFileOverlay_create : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_VirtualFileOverlay_create(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_VirtualFileOverlay_create",
        [=](unsigned int options) {
          return pybind11_weaver::WrapP<CXVirtualFileOverlayImpl *>(
                     clang_VirtualFileOverlay_create(options))
              .release();
        },
        R"_pb11_weaver(/**
 * Create a \c CXVirtualFileOverlay object.
 * Must be disposed with \c clang_VirtualFileOverlay_dispose().
 *
 * \param options is reserved, always pass 0.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_VirtualFileOverlay_create"; }
};

struct Entity_clang_VirtualFileOverlay_create
    : public Bind_clang_VirtualFileOverlay_create<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_VirtualFileOverlay_create(EntityScope parent_h)
      : Bind_clang_VirtualFileOverlay_create<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_create

#ifndef PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_addFileMapping
template <class Pybind11T>
struct Bind_clang_VirtualFileOverlay_addFileMapping : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_VirtualFileOverlay_addFileMapping(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_VirtualFileOverlay_addFileMapping",
        [=](pybind11_weaver::PointerWrapper<CXVirtualFileOverlayImpl *> *arg0,
            const char *virtualPath, const char *realPath) {
          return clang_VirtualFileOverlay_addFileMapping((arg0)->ptr,
                                                         virtualPath, realPath);
        },
        R"_pb11_weaver(/**
 * Map an absolute virtual file path to an absolute real one.
 * The virtual path must be canonicalized (not contain "."/"..").
 * \returns 0 for success, non-zero to indicate an error.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_VirtualFileOverlay_addFileMapping"; }
};

struct Entity_clang_VirtualFileOverlay_addFileMapping
    : public Bind_clang_VirtualFileOverlay_addFileMapping<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_VirtualFileOverlay_addFileMapping(EntityScope parent_h)
      : Bind_clang_VirtualFileOverlay_addFileMapping<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_addFileMapping

#ifndef PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_setCaseSensitivity
template <class Pybind11T>
struct Bind_clang_VirtualFileOverlay_setCaseSensitivity : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_VirtualFileOverlay_setCaseSensitivity(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_VirtualFileOverlay_setCaseSensitivity",
        [=](pybind11_weaver::PointerWrapper<CXVirtualFileOverlayImpl *> *arg0,
            int caseSensitive) {
          return clang_VirtualFileOverlay_setCaseSensitivity((arg0)->ptr,
                                                             caseSensitive);
        },
        R"_pb11_weaver(/**
 * Set the case sensitivity for the \c CXVirtualFileOverlay object.
 * The \c CXVirtualFileOverlay object is case-sensitive by default, this
 * option can be used to override the default.
 * \returns 0 for success, non-zero to indicate an error.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_VirtualFileOverlay_setCaseSensitivity";
  }
};

struct Entity_clang_VirtualFileOverlay_setCaseSensitivity
    : public Bind_clang_VirtualFileOverlay_setCaseSensitivity<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_VirtualFileOverlay_setCaseSensitivity(
      EntityScope parent_h)
      : Bind_clang_VirtualFileOverlay_setCaseSensitivity<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_setCaseSensitivity

#ifndef PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_writeToBuffer
template <class Pybind11T>
struct Bind_clang_VirtualFileOverlay_writeToBuffer : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_VirtualFileOverlay_writeToBuffer(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_VirtualFileOverlay_writeToBuffer",
        [=](pybind11_weaver::PointerWrapper<CXVirtualFileOverlayImpl *> *arg0,
            unsigned int options,
            pybind11_weaver::PointerWrapper<char **> *out_buffer_ptr,
            unsigned int *out_buffer_size) {
          return clang_VirtualFileOverlay_writeToBuffer(
              (arg0)->ptr, options, (out_buffer_ptr)->ptr, out_buffer_size);
        },
        R"_pb11_weaver(/**
 * Write out the \c CXVirtualFileOverlay object to a char buffer.
 *
 * \param options is reserved, always pass 0.
 * \param out_buffer_ptr pointer to receive the buffer pointer, which should be
 * disposed using \c clang_free().
 * \param out_buffer_size pointer to receive the buffer size.
 * \returns 0 for success, non-zero to indicate an error.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_VirtualFileOverlay_writeToBuffer"; }
};

struct Entity_clang_VirtualFileOverlay_writeToBuffer
    : public Bind_clang_VirtualFileOverlay_writeToBuffer<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_VirtualFileOverlay_writeToBuffer(EntityScope parent_h)
      : Bind_clang_VirtualFileOverlay_writeToBuffer<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_writeToBuffer

#ifndef PB11_WEAVER_DISABLE_Entity_clang_free
template <class Pybind11T> struct Bind_clang_free : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_free(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_free",
        [=](pybind11_weaver::PointerWrapper<void *> *buffer) {
          return clang_free((buffer)->ptr);
        },
        R"_pb11_weaver(/**
 * free memory allocated by libclang, such as the buffer returned by
 * \c CXVirtualFileOverlay() or \c clang_ModuleMapDescriptor_writeToBuffer().
 *
 * \param buffer memory pointer to free.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_free"; }
};

struct Entity_clang_free
    : public Bind_clang_free<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_free(EntityScope parent_h)
      : Bind_clang_free<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_free

#ifndef PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_dispose
template <class Pybind11T>
struct Bind_clang_VirtualFileOverlay_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_VirtualFileOverlay_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_VirtualFileOverlay_dispose",
        [=](pybind11_weaver::PointerWrapper<CXVirtualFileOverlayImpl *> *arg0) {
          return clang_VirtualFileOverlay_dispose((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Dispose a \c CXVirtualFileOverlay object.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_VirtualFileOverlay_dispose"; }
};

struct Entity_clang_VirtualFileOverlay_dispose
    : public Bind_clang_VirtualFileOverlay_dispose<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_VirtualFileOverlay_dispose(EntityScope parent_h)
      : Bind_clang_VirtualFileOverlay_dispose<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_VirtualFileOverlay_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_create
template <class Pybind11T>
struct Bind_clang_ModuleMapDescriptor_create : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ModuleMapDescriptor_create(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_ModuleMapDescriptor_create",
        [=](unsigned int options) {
          return pybind11_weaver::WrapP<CXModuleMapDescriptorImpl *>(
                     clang_ModuleMapDescriptor_create(options))
              .release();
        },
        R"_pb11_weaver(/**
 * Create a \c CXModuleMapDescriptor object.
 * Must be disposed with \c clang_ModuleMapDescriptor_dispose().
 *
 * \param options is reserved, always pass 0.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_ModuleMapDescriptor_create"; }
};

struct Entity_clang_ModuleMapDescriptor_create
    : public Bind_clang_ModuleMapDescriptor_create<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ModuleMapDescriptor_create(EntityScope parent_h)
      : Bind_clang_ModuleMapDescriptor_create<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_create

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_setFrameworkModuleName
template <class Pybind11T>
struct Bind_clang_ModuleMapDescriptor_setFrameworkModuleName
    : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ModuleMapDescriptor_setFrameworkModuleName(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_ModuleMapDescriptor_setFrameworkModuleName",
        [=](pybind11_weaver::PointerWrapper<CXModuleMapDescriptorImpl *> *arg0,
            const char *name) {
          return clang_ModuleMapDescriptor_setFrameworkModuleName((arg0)->ptr,
                                                                  name);
        },
        R"_pb11_weaver(/**
 * Sets the framework module name that the module.map describes.
 * \returns 0 for success, non-zero to indicate an error.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_ModuleMapDescriptor_setFrameworkModuleName";
  }
};

struct Entity_clang_ModuleMapDescriptor_setFrameworkModuleName
    : public Bind_clang_ModuleMapDescriptor_setFrameworkModuleName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ModuleMapDescriptor_setFrameworkModuleName(
      EntityScope parent_h)
      : Bind_clang_ModuleMapDescriptor_setFrameworkModuleName<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_setFrameworkModuleName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_setUmbrellaHeader
template <class Pybind11T>
struct Bind_clang_ModuleMapDescriptor_setUmbrellaHeader : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ModuleMapDescriptor_setUmbrellaHeader(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_ModuleMapDescriptor_setUmbrellaHeader",
        [=](pybind11_weaver::PointerWrapper<CXModuleMapDescriptorImpl *> *arg0,
            const char *name) {
          return clang_ModuleMapDescriptor_setUmbrellaHeader((arg0)->ptr, name);
        },
        R"_pb11_weaver(/**
 * Sets the umbrella header name that the module.map describes.
 * \returns 0 for success, non-zero to indicate an error.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_ModuleMapDescriptor_setUmbrellaHeader";
  }
};

struct Entity_clang_ModuleMapDescriptor_setUmbrellaHeader
    : public Bind_clang_ModuleMapDescriptor_setUmbrellaHeader<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ModuleMapDescriptor_setUmbrellaHeader(
      EntityScope parent_h)
      : Bind_clang_ModuleMapDescriptor_setUmbrellaHeader<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_setUmbrellaHeader

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_writeToBuffer
template <class Pybind11T>
struct Bind_clang_ModuleMapDescriptor_writeToBuffer : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ModuleMapDescriptor_writeToBuffer(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_ModuleMapDescriptor_writeToBuffer",
        [=](pybind11_weaver::PointerWrapper<CXModuleMapDescriptorImpl *> *arg0,
            unsigned int options,
            pybind11_weaver::PointerWrapper<char **> *out_buffer_ptr,
            unsigned int *out_buffer_size) {
          return clang_ModuleMapDescriptor_writeToBuffer(
              (arg0)->ptr, options, (out_buffer_ptr)->ptr, out_buffer_size);
        },
        R"_pb11_weaver(/**
 * Write out the \c CXModuleMapDescriptor object to a char buffer.
 *
 * \param options is reserved, always pass 0.
 * \param out_buffer_ptr pointer to receive the buffer pointer, which should be
 * disposed using \c clang_free().
 * \param out_buffer_size pointer to receive the buffer size.
 * \returns 0 for success, non-zero to indicate an error.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_ModuleMapDescriptor_writeToBuffer"; }
};

struct Entity_clang_ModuleMapDescriptor_writeToBuffer
    : public Bind_clang_ModuleMapDescriptor_writeToBuffer<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ModuleMapDescriptor_writeToBuffer(EntityScope parent_h)
      : Bind_clang_ModuleMapDescriptor_writeToBuffer<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_writeToBuffer

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_dispose
template <class Pybind11T>
struct Bind_clang_ModuleMapDescriptor_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ModuleMapDescriptor_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_ModuleMapDescriptor_dispose",
        [=](pybind11_weaver::PointerWrapper<CXModuleMapDescriptorImpl *>
                *arg0) {
          return clang_ModuleMapDescriptor_dispose((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Dispose a \c CXModuleMapDescriptor object.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_ModuleMapDescriptor_dispose"; }
};

struct Entity_clang_ModuleMapDescriptor_dispose
    : public Bind_clang_ModuleMapDescriptor_dispose<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ModuleMapDescriptor_dispose(EntityScope parent_h)
      : Bind_clang_ModuleMapDescriptor_dispose<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ModuleMapDescriptor_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_CXCompilationDatabase_Error
template <class Pybind11T>
struct Bind_CXCompilationDatabase_Error : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCompilationDatabase_Error(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCompilationDatabase_NoError",
                   CXCompilationDatabase_Error::CXCompilationDatabase_NoError,
                   R"_pb11_weaver(/*
   * No error occurred
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCompilationDatabase_CanNotLoadDatabase",
        CXCompilationDatabase_Error::CXCompilationDatabase_CanNotLoadDatabase,
        R"_pb11_weaver(/*
   * Database can not be loaded
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCompilationDatabase_Error"; }
};

struct Entity_CXCompilationDatabase_Error
    : public Bind_CXCompilationDatabase_Error<
          std::decay_t<pybind11::enum_<CXCompilationDatabase_Error>>> {

  explicit Entity_CXCompilationDatabase_Error(EntityScope parent_h)
      : Bind_CXCompilationDatabase_Error<
            std::decay_t<pybind11::enum_<CXCompilationDatabase_Error>>>(
            parent_h),
        handle{parent_h, "CXCompilationDatabase_Error", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Error codes for Compilation Database
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCompilationDatabase_Error> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCompilationDatabase_Error

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompilationDatabase_fromDirectory
template <class Pybind11T>
struct Bind_clang_CompilationDatabase_fromDirectory : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompilationDatabase_fromDirectory(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompilationDatabase_fromDirectory",
        [=](const char *BuildDir, CXCompilationDatabase_Error *ErrorCode) {
          return pybind11_weaver::WrapP<void *>(
                     clang_CompilationDatabase_fromDirectory(BuildDir,
                                                             ErrorCode))
              .release();
        },
        R"_pb11_weaver(/**
 * Creates a compilation database from the database found in directory
 * buildDir. For example, CMake can output a compile_commands.json which can
 * be used to build the database.
 *
 * It must be freed by \c clang_CompilationDatabase_dispose.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompilationDatabase_fromDirectory"; }
};

struct Entity_clang_CompilationDatabase_fromDirectory
    : public Bind_clang_CompilationDatabase_fromDirectory<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompilationDatabase_fromDirectory(EntityScope parent_h)
      : Bind_clang_CompilationDatabase_fromDirectory<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompilationDatabase_fromDirectory

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompilationDatabase_dispose
template <class Pybind11T>
struct Bind_clang_CompilationDatabase_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompilationDatabase_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompilationDatabase_dispose",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_CompilationDatabase_dispose((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Free the given compilation database
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompilationDatabase_dispose"; }
};

struct Entity_clang_CompilationDatabase_dispose
    : public Bind_clang_CompilationDatabase_dispose<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompilationDatabase_dispose(EntityScope parent_h)
      : Bind_clang_CompilationDatabase_dispose<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompilationDatabase_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompilationDatabase_getCompileCommands
template <class Pybind11T>
struct Bind_clang_CompilationDatabase_getCompileCommands : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompilationDatabase_getCompileCommands(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompilationDatabase_getCompileCommands",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0,
            const char *CompleteFileName) {
          return pybind11_weaver::WrapP<void *>(
                     clang_CompilationDatabase_getCompileCommands(
                         (arg0)->ptr, CompleteFileName))
              .release();
        },
        R"_pb11_weaver(/**
 * Find the compile commands used for a file. The compile commands
 * must be freed by \c clang_CompileCommands_dispose.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CompilationDatabase_getCompileCommands";
  }
};

struct Entity_clang_CompilationDatabase_getCompileCommands
    : public Bind_clang_CompilationDatabase_getCompileCommands<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompilationDatabase_getCompileCommands(
      EntityScope parent_h)
      : Bind_clang_CompilationDatabase_getCompileCommands<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompilationDatabase_getCompileCommands

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompilationDatabase_getAllCompileCommands
template <class Pybind11T>
struct Bind_clang_CompilationDatabase_getAllCompileCommands
    : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompilationDatabase_getAllCompileCommands(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompilationDatabase_getAllCompileCommands",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return pybind11_weaver::WrapP<void *>(
                     clang_CompilationDatabase_getAllCompileCommands(
                         (arg0)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * Get all the compile commands in the given compilation database.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CompilationDatabase_getAllCompileCommands";
  }
};

struct Entity_clang_CompilationDatabase_getAllCompileCommands
    : public Bind_clang_CompilationDatabase_getAllCompileCommands<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompilationDatabase_getAllCompileCommands(
      EntityScope parent_h)
      : Bind_clang_CompilationDatabase_getAllCompileCommands<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompilationDatabase_getAllCompileCommands

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommands_dispose
template <class Pybind11T>
struct Bind_clang_CompileCommands_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommands_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommands_dispose",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_CompileCommands_dispose((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Free the given CompileCommands
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompileCommands_dispose"; }
};

struct Entity_clang_CompileCommands_dispose
    : public Bind_clang_CompileCommands_dispose<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommands_dispose(EntityScope parent_h)
      : Bind_clang_CompileCommands_dispose<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommands_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommands_getSize
template <class Pybind11T>
struct Bind_clang_CompileCommands_getSize : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommands_getSize(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommands_getSize",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_CompileCommands_getSize((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Get the number of CompileCommand we have for a file
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompileCommands_getSize"; }
};

struct Entity_clang_CompileCommands_getSize
    : public Bind_clang_CompileCommands_getSize<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommands_getSize(EntityScope parent_h)
      : Bind_clang_CompileCommands_getSize<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommands_getSize

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommands_getCommand
template <class Pybind11T>
struct Bind_clang_CompileCommands_getCommand : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommands_getCommand(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommands_getCommand",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0, unsigned int I) {
          return pybind11_weaver::WrapP<void *>(
                     clang_CompileCommands_getCommand((arg0)->ptr, I))
              .release();
        },
        R"_pb11_weaver(/**
 * Get the I'th CompileCommand for a file
 *
 * Note : 0 <= i < clang_CompileCommands_getSize(CXCompileCommands)
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompileCommands_getCommand"; }
};

struct Entity_clang_CompileCommands_getCommand
    : public Bind_clang_CompileCommands_getCommand<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommands_getCommand(EntityScope parent_h)
      : Bind_clang_CompileCommands_getCommand<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommands_getCommand

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getDirectory
template <class Pybind11T>
struct Bind_clang_CompileCommand_getDirectory : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommand_getDirectory(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommand_getDirectory",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_CompileCommand_getDirectory((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Get the working directory where the CompileCommand was executed from
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompileCommand_getDirectory"; }
};

struct Entity_clang_CompileCommand_getDirectory
    : public Bind_clang_CompileCommand_getDirectory<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommand_getDirectory(EntityScope parent_h)
      : Bind_clang_CompileCommand_getDirectory<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getDirectory

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getFilename
template <class Pybind11T>
struct Bind_clang_CompileCommand_getFilename : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommand_getFilename(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommand_getFilename",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_CompileCommand_getFilename((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Get the filename associated with the CompileCommand.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompileCommand_getFilename"; }
};

struct Entity_clang_CompileCommand_getFilename
    : public Bind_clang_CompileCommand_getFilename<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommand_getFilename(EntityScope parent_h)
      : Bind_clang_CompileCommand_getFilename<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getFilename

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getNumArgs
template <class Pybind11T>
struct Bind_clang_CompileCommand_getNumArgs : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommand_getNumArgs(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommand_getNumArgs",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_CompileCommand_getNumArgs((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Get the number of arguments in the compiler invocation.
 *
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompileCommand_getNumArgs"; }
};

struct Entity_clang_CompileCommand_getNumArgs
    : public Bind_clang_CompileCommand_getNumArgs<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommand_getNumArgs(EntityScope parent_h)
      : Bind_clang_CompileCommand_getNumArgs<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getNumArgs

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getArg
template <class Pybind11T>
struct Bind_clang_CompileCommand_getArg : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommand_getArg(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommand_getArg",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0, unsigned int I) {
          return clang_CompileCommand_getArg((arg0)->ptr, I);
        },
        R"_pb11_weaver(/**
 * Get the I'th argument value in the compiler invocations
 *
 * Invariant :
 *  - argument 0 is the compiler executable
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CompileCommand_getArg"; }
};

struct Entity_clang_CompileCommand_getArg
    : public Bind_clang_CompileCommand_getArg<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommand_getArg(EntityScope parent_h)
      : Bind_clang_CompileCommand_getArg<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getArg

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getNumMappedSources
template <class Pybind11T>
struct Bind_clang_CompileCommand_getNumMappedSources : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommand_getNumMappedSources(EntityScope parent_h) {
  }

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommand_getNumMappedSources",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_CompileCommand_getNumMappedSources((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Get the number of source mappings for the compiler invocation.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CompileCommand_getNumMappedSources";
  }
};

struct Entity_clang_CompileCommand_getNumMappedSources
    : public Bind_clang_CompileCommand_getNumMappedSources<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommand_getNumMappedSources(EntityScope parent_h)
      : Bind_clang_CompileCommand_getNumMappedSources<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getNumMappedSources

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getMappedSourcePath
template <class Pybind11T>
struct Bind_clang_CompileCommand_getMappedSourcePath : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommand_getMappedSourcePath(EntityScope parent_h) {
  }

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommand_getMappedSourcePath",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0, unsigned int I) {
          return clang_CompileCommand_getMappedSourcePath((arg0)->ptr, I);
        },
        R"_pb11_weaver(/**
 * Get the I'th mapped source path for the compiler invocation.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CompileCommand_getMappedSourcePath";
  }
};

struct Entity_clang_CompileCommand_getMappedSourcePath
    : public Bind_clang_CompileCommand_getMappedSourcePath<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommand_getMappedSourcePath(EntityScope parent_h)
      : Bind_clang_CompileCommand_getMappedSourcePath<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getMappedSourcePath

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getMappedSourceContent
template <class Pybind11T>
struct Bind_clang_CompileCommand_getMappedSourceContent : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CompileCommand_getMappedSourceContent(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CompileCommand_getMappedSourceContent",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0, unsigned int I) {
          return clang_CompileCommand_getMappedSourceContent((arg0)->ptr, I);
        },
        R"_pb11_weaver(/**
 * Get the I'th mapped source content for the compiler invocation.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CompileCommand_getMappedSourceContent";
  }
};

struct Entity_clang_CompileCommand_getMappedSourceContent
    : public Bind_clang_CompileCommand_getMappedSourceContent<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CompileCommand_getMappedSourceContent(
      EntityScope parent_h)
      : Bind_clang_CompileCommand_getMappedSourceContent<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CompileCommand_getMappedSourceContent

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getFileName
template <class Pybind11T> struct Bind_clang_getFileName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getFileName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getFileName",
        [=](pybind11_weaver::PointerWrapper<void *> *SFile) {
          return clang_getFileName((SFile)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the complete file and path name of the given file.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getFileName"; }
};

struct Entity_clang_getFileName
    : public Bind_clang_getFileName<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getFileName(EntityScope parent_h)
      : Bind_clang_getFileName<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getFileName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getFileTime
template <class Pybind11T> struct Bind_clang_getFileTime : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getFileTime(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getFileTime",
        [=](pybind11_weaver::PointerWrapper<void *> *SFile) {
          return clang_getFileTime((SFile)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the last modification time of the given file.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getFileTime"; }
};

struct Entity_clang_getFileTime
    : public Bind_clang_getFileTime<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getFileTime(EntityScope parent_h)
      : Bind_clang_getFileTime<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getFileTime

#ifndef PB11_WEAVER_DISABLE_Entity_CXFileUniqueID
template <class Pybind11T> struct Bind_CXFileUniqueID : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXFileUniqueID(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXFileUniqueID"; }
};

struct Entity_CXFileUniqueID
    : public Bind_CXFileUniqueID<
          std::decay_t<pybind11::class_<CXFileUniqueID>>> {

  explicit Entity_CXFileUniqueID(EntityScope parent_h)
      : Bind_CXFileUniqueID<std::decay_t<pybind11::class_<CXFileUniqueID>>>(
            parent_h),
        handle{parent_h, "CXFileUniqueID", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Uniquely identifies a CXFile, that refers to the same underlying file,
 * across an indexing session.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXFileUniqueID> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXFileUniqueID

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getFileUniqueID
template <class Pybind11T>
struct Bind_clang_getFileUniqueID : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getFileUniqueID(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getFileUniqueID",
        [=](pybind11_weaver::PointerWrapper<void *> *file,
            CXFileUniqueID *outID) {
          return clang_getFileUniqueID((file)->ptr, outID);
        },
        R"_pb11_weaver(/**
 * Retrieve the unique ID for the given \c file.
 *
 * \param file the file to get the ID for.
 * \param outID stores the returned CXFileUniqueID.
 * \returns If there was a failure getting the unique ID, returns non-zero,
 * otherwise returns 0.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getFileUniqueID"; }
};

struct Entity_clang_getFileUniqueID
    : public Bind_clang_getFileUniqueID<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getFileUniqueID(EntityScope parent_h)
      : Bind_clang_getFileUniqueID<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getFileUniqueID

#ifndef PB11_WEAVER_DISABLE_Entity_clang_File_isEqual
template <class Pybind11T> struct Bind_clang_File_isEqual : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_File_isEqual(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_File_isEqual",
        [=](pybind11_weaver::PointerWrapper<void *> *file1,
            pybind11_weaver::PointerWrapper<void *> *file2) {
          return clang_File_isEqual((file1)->ptr, (file2)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns non-zero if the \c file1 and \c file2 point to the same file,
 * or they are both NULL.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_File_isEqual"; }
};

struct Entity_clang_File_isEqual
    : public Bind_clang_File_isEqual<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_File_isEqual(EntityScope parent_h)
      : Bind_clang_File_isEqual<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_File_isEqual

#ifndef PB11_WEAVER_DISABLE_Entity_clang_File_tryGetRealPathName
template <class Pybind11T>
struct Bind_clang_File_tryGetRealPathName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_File_tryGetRealPathName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_File_tryGetRealPathName",
        [=](pybind11_weaver::PointerWrapper<void *> *file) {
          return clang_File_tryGetRealPathName((file)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the real path name of \c file.
 *
 * An empty string may be returned. Use \c clang_getFileName() in that case.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_File_tryGetRealPathName"; }
};

struct Entity_clang_File_tryGetRealPathName
    : public Bind_clang_File_tryGetRealPathName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_File_tryGetRealPathName(EntityScope parent_h)
      : Bind_clang_File_tryGetRealPathName<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_File_tryGetRealPathName

#ifndef PB11_WEAVER_DISABLE_Entity_CXSourceLocation
template <class Pybind11T> struct Bind_CXSourceLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXSourceLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("int_data", &CXSourceLocation::int_data);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXSourceLocation"; }
};

struct Entity_CXSourceLocation
    : public Bind_CXSourceLocation<
          std::decay_t<pybind11::class_<CXSourceLocation>>> {

  explicit Entity_CXSourceLocation(EntityScope parent_h)
      : Bind_CXSourceLocation<std::decay_t<pybind11::class_<CXSourceLocation>>>(
            parent_h),
        handle{parent_h, "CXSourceLocation", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Identifies a specific source location within a translation
 * unit.
 *
 * Use clang_getExpansionLocation() or clang_getSpellingLocation()
 * to map a source location to a particular file, line, and column.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXSourceLocation> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXSourceLocation

#ifndef PB11_WEAVER_DISABLE_Entity_CXSourceRange
template <class Pybind11T> struct Bind_CXSourceRange : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXSourceRange(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("begin_int_data", &CXSourceRange::begin_int_data);
    pb11_obj.def_readwrite("end_int_data", &CXSourceRange::end_int_data);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXSourceRange"; }
};

struct Entity_CXSourceRange
    : public Bind_CXSourceRange<std::decay_t<pybind11::class_<CXSourceRange>>> {

  explicit Entity_CXSourceRange(EntityScope parent_h)
      : Bind_CXSourceRange<std::decay_t<pybind11::class_<CXSourceRange>>>(
            parent_h),
        handle{parent_h, "CXSourceRange", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Identifies a half-open character range in the source code.
 *
 * Use clang_getRangeStart() and clang_getRangeEnd() to retrieve the
 * starting and end locations from a source range, respectively.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXSourceRange> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXSourceRange

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNullLocation
template <class Pybind11T>
struct Bind_clang_getNullLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNullLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getNullLocation",
                 static_cast<CXSourceLocation (*)()>(&clang_getNullLocation),
                 R"_pb11_weaver(/**
 * Retrieve a NULL (invalid) source location.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNullLocation"; }
};

struct Entity_clang_getNullLocation
    : public Bind_clang_getNullLocation<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNullLocation(EntityScope parent_h)
      : Bind_clang_getNullLocation<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNullLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_equalLocations
template <class Pybind11T>
struct Bind_clang_equalLocations : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_equalLocations(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_equalLocations",
        static_cast<unsigned int (*)(CXSourceLocation, CXSourceLocation)>(
            &clang_equalLocations),
        R"_pb11_weaver(/**
 * Determine whether two source locations, which must refer into
 * the same translation unit, refer to exactly the same point in the source
 * code.
 *
 * \returns non-zero if the source locations refer to the same location, zero
 * if they refer to different locations.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_equalLocations"; }
};

struct Entity_clang_equalLocations
    : public Bind_clang_equalLocations<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_equalLocations(EntityScope parent_h)
      : Bind_clang_equalLocations<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_equalLocations

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Location_isInSystemHeader
template <class Pybind11T>
struct Bind_clang_Location_isInSystemHeader : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Location_isInSystemHeader(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Location_isInSystemHeader",
                 static_cast<int (*)(CXSourceLocation)>(
                     &clang_Location_isInSystemHeader),
                 R"_pb11_weaver(/**
 * Returns non-zero if the given source location is in a system header.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Location_isInSystemHeader"; }
};

struct Entity_clang_Location_isInSystemHeader
    : public Bind_clang_Location_isInSystemHeader<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Location_isInSystemHeader(EntityScope parent_h)
      : Bind_clang_Location_isInSystemHeader<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Location_isInSystemHeader

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Location_isFromMainFile
template <class Pybind11T>
struct Bind_clang_Location_isFromMainFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Location_isFromMainFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Location_isFromMainFile",
        static_cast<int (*)(CXSourceLocation)>(&clang_Location_isFromMainFile),
        R"_pb11_weaver(/**
 * Returns non-zero if the given source location is in the main file of
 * the corresponding translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Location_isFromMainFile"; }
};

struct Entity_clang_Location_isFromMainFile
    : public Bind_clang_Location_isFromMainFile<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Location_isFromMainFile(EntityScope parent_h)
      : Bind_clang_Location_isFromMainFile<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Location_isFromMainFile

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNullRange
template <class Pybind11T> struct Bind_clang_getNullRange : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNullRange(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getNullRange",
                 static_cast<CXSourceRange (*)()>(&clang_getNullRange),
                 R"_pb11_weaver(/**
 * Retrieve a NULL (invalid) source range.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNullRange"; }
};

struct Entity_clang_getNullRange
    : public Bind_clang_getNullRange<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNullRange(EntityScope parent_h)
      : Bind_clang_getNullRange<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNullRange

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getRange
template <class Pybind11T> struct Bind_clang_getRange : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getRange(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getRange",
        static_cast<CXSourceRange (*)(CXSourceLocation, CXSourceLocation)>(
            &clang_getRange),
        R"_pb11_weaver(/**
 * Retrieve a source range given the beginning and ending source
 * locations.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getRange"; }
};

struct Entity_clang_getRange
    : public Bind_clang_getRange<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getRange(EntityScope parent_h)
      : Bind_clang_getRange<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getRange

#ifndef PB11_WEAVER_DISABLE_Entity_clang_equalRanges
template <class Pybind11T> struct Bind_clang_equalRanges : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_equalRanges(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_equalRanges",
                 static_cast<unsigned int (*)(CXSourceRange, CXSourceRange)>(
                     &clang_equalRanges),
                 R"_pb11_weaver(/**
 * Determine whether two ranges are equivalent.
 *
 * \returns non-zero if the ranges are the same, zero if they differ.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_equalRanges"; }
};

struct Entity_clang_equalRanges
    : public Bind_clang_equalRanges<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_equalRanges(EntityScope parent_h)
      : Bind_clang_equalRanges<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_equalRanges

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Range_isNull
template <class Pybind11T> struct Bind_clang_Range_isNull : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Range_isNull(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Range_isNull",
                 static_cast<int (*)(CXSourceRange)>(&clang_Range_isNull),
                 R"_pb11_weaver(/**
 * Returns non-zero if \p range is null.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Range_isNull"; }
};

struct Entity_clang_Range_isNull
    : public Bind_clang_Range_isNull<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Range_isNull(EntityScope parent_h)
      : Bind_clang_Range_isNull<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Range_isNull

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getExpansionLocation
template <class Pybind11T>
struct Bind_clang_getExpansionLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getExpansionLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getExpansionLocation",
        [=](CXSourceLocation location,
            pybind11_weaver::PointerWrapper<void **> *file, unsigned int *line,
            unsigned int *column, unsigned int *offset) {
          return clang_getExpansionLocation(location, (file)->ptr, line, column,
                                            offset);
        },
        R"_pb11_weaver(/**
 * Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro expansion, retrieves the
 * location of the macro expansion.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getExpansionLocation"; }
};

struct Entity_clang_getExpansionLocation
    : public Bind_clang_getExpansionLocation<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getExpansionLocation(EntityScope parent_h)
      : Bind_clang_getExpansionLocation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getExpansionLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getPresumedLocation
template <class Pybind11T>
struct Bind_clang_getPresumedLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getPresumedLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getPresumedLocation",
        static_cast<void (*)(CXSourceLocation, CXString *, unsigned int *,
                             unsigned int *)>(&clang_getPresumedLocation),
        R"_pb11_weaver(/**
 * Retrieve the file, line and column represented by the given source
 * location, as specified in a # line directive.
 *
 * Example: given the following source code in a file somefile.c
 *
 * \code
 * #123 "dummy.c" 1
 *
 * static int func(void)
 * {
 *     return 0;
 * }
 * \endcode
 *
 * the location information returned by this function would be
 *
 * File: dummy.c Line: 124 Column: 12
 *
 * whereas clang_getExpansionLocation would have returned
 *
 * File: somefile.c Line: 3 Column: 12
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param filename [out] if non-NULL, will be set to the filename of the
 * source location. Note that filenames returned will be for "virtual" files,
 * which don't necessarily exist on the machine running clang - e.g. when
 * parsing preprocessed output obtained from a different environment. If
 * a non-NULL value is passed in, remember to dispose of the returned value
 * using \c clang_disposeString() once you've finished with it. For an invalid
 * source location, an empty string is returned.
 *
 * \param line [out] if non-NULL, will be set to the line number of the
 * source location. For an invalid source location, zero is returned.
 *
 * \param column [out] if non-NULL, will be set to the column number of the
 * source location. For an invalid source location, zero is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getPresumedLocation"; }
};

struct Entity_clang_getPresumedLocation
    : public Bind_clang_getPresumedLocation<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getPresumedLocation(EntityScope parent_h)
      : Bind_clang_getPresumedLocation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getPresumedLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getInstantiationLocation
template <class Pybind11T>
struct Bind_clang_getInstantiationLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getInstantiationLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getInstantiationLocation",
        [=](CXSourceLocation location,
            pybind11_weaver::PointerWrapper<void **> *file, unsigned int *line,
            unsigned int *column, unsigned int *offset) {
          return clang_getInstantiationLocation(location, (file)->ptr, line,
                                                column, offset);
        },
        R"_pb11_weaver(/**
 * Legacy API to retrieve the file, line, column, and offset represented
 * by the given source location.
 *
 * This interface has been replaced by the newer interface
 * #clang_getExpansionLocation(). See that interface's documentation for
 * details.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getInstantiationLocation"; }
};

struct Entity_clang_getInstantiationLocation
    : public Bind_clang_getInstantiationLocation<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getInstantiationLocation(EntityScope parent_h)
      : Bind_clang_getInstantiationLocation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getInstantiationLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getSpellingLocation
template <class Pybind11T>
struct Bind_clang_getSpellingLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getSpellingLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getSpellingLocation",
        [=](CXSourceLocation location,
            pybind11_weaver::PointerWrapper<void **> *file, unsigned int *line,
            unsigned int *column, unsigned int *offset) {
          return clang_getSpellingLocation(location, (file)->ptr, line, column,
                                           offset);
        },
        R"_pb11_weaver(/**
 * Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro instantiation, return where the
 * location was originally spelled in the source file.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getSpellingLocation"; }
};

struct Entity_clang_getSpellingLocation
    : public Bind_clang_getSpellingLocation<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getSpellingLocation(EntityScope parent_h)
      : Bind_clang_getSpellingLocation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getSpellingLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getFileLocation
template <class Pybind11T>
struct Bind_clang_getFileLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getFileLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getFileLocation",
        [=](CXSourceLocation location,
            pybind11_weaver::PointerWrapper<void **> *file, unsigned int *line,
            unsigned int *column, unsigned int *offset) {
          return clang_getFileLocation(location, (file)->ptr, line, column,
                                       offset);
        },
        R"_pb11_weaver(/**
 * Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro expansion, return where the macro was
 * expanded or where the macro argument was written, if the location points at
 * a macro argument.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getFileLocation"; }
};

struct Entity_clang_getFileLocation
    : public Bind_clang_getFileLocation<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getFileLocation(EntityScope parent_h)
      : Bind_clang_getFileLocation<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getFileLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getRangeStart
template <class Pybind11T> struct Bind_clang_getRangeStart : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getRangeStart(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getRangeStart",
        static_cast<CXSourceLocation (*)(CXSourceRange)>(&clang_getRangeStart),
        R"_pb11_weaver(/**
 * Retrieve a source location representing the first character within a
 * source range.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getRangeStart"; }
};

struct Entity_clang_getRangeStart
    : public Bind_clang_getRangeStart<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getRangeStart(EntityScope parent_h)
      : Bind_clang_getRangeStart<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getRangeStart

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getRangeEnd
template <class Pybind11T> struct Bind_clang_getRangeEnd : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getRangeEnd(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getRangeEnd",
        static_cast<CXSourceLocation (*)(CXSourceRange)>(&clang_getRangeEnd),
        R"_pb11_weaver(/**
 * Retrieve a source location representing the last character within a
 * source range.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getRangeEnd"; }
};

struct Entity_clang_getRangeEnd
    : public Bind_clang_getRangeEnd<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getRangeEnd(EntityScope parent_h)
      : Bind_clang_getRangeEnd<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getRangeEnd

#ifndef PB11_WEAVER_DISABLE_Entity_CXSourceRangeList
template <class Pybind11T> struct Bind_CXSourceRangeList : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXSourceRangeList(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * An array of \c CXSourceRanges.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite(
        "count", &CXSourceRangeList::count,
        R"_pb11_weaver(/** The number of ranges in the \c ranges array. */)_pb11_weaver");
    pb11_obj.def_readwrite("ranges", &CXSourceRangeList::ranges,
                           R"_pb11_weaver(/**
   * An array of \c CXSourceRanges.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXSourceRangeList"; }
};

struct Entity_CXSourceRangeList
    : public Bind_CXSourceRangeList<
          std::decay_t<pybind11::class_<CXSourceRangeList>>> {

  explicit Entity_CXSourceRangeList(EntityScope parent_h)
      : Bind_CXSourceRangeList<
            std::decay_t<pybind11::class_<CXSourceRangeList>>>(parent_h),
        handle{parent_h, "CXSourceRangeList", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Identifies an array of ranges.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXSourceRangeList> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXSourceRangeList

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeSourceRangeList
template <class Pybind11T>
struct Bind_clang_disposeSourceRangeList : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeSourceRangeList(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_disposeSourceRangeList",
                 static_cast<void (*)(CXSourceRangeList *)>(
                     &clang_disposeSourceRangeList),
                 R"_pb11_weaver(/**
 * Destroy the given \c CXSourceRangeList.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeSourceRangeList"; }
};

struct Entity_clang_disposeSourceRangeList
    : public Bind_clang_disposeSourceRangeList<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeSourceRangeList(EntityScope parent_h)
      : Bind_clang_disposeSourceRangeList<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeSourceRangeList

#ifndef PB11_WEAVER_DISABLE_Entity_CXDiagnosticSeverity
template <class Pybind11T>
struct Bind_CXDiagnosticSeverity : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXDiagnosticSeverity(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXDiagnostic_Ignored",
                   CXDiagnosticSeverity::CXDiagnostic_Ignored,
                   R"_pb11_weaver(/**
   * A diagnostic that has been suppressed, e.g., by a command-line
   * option.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_Note", CXDiagnosticSeverity::CXDiagnostic_Note,
                   R"_pb11_weaver(/**
   * This diagnostic is a note that should be attached to the
   * previous (non-note) diagnostic.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_Warning",
                   CXDiagnosticSeverity::CXDiagnostic_Warning,
                   R"_pb11_weaver(/**
   * This diagnostic indicates suspicious code that may not be
   * wrong.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_Error",
                   CXDiagnosticSeverity::CXDiagnostic_Error, R"_pb11_weaver(/**
   * This diagnostic indicates that the code is ill-formed.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_Fatal",
                   CXDiagnosticSeverity::CXDiagnostic_Fatal, R"_pb11_weaver(/**
   * This diagnostic indicates that the code is ill-formed such
   * that future parser recovery is unlikely to produce useful
   * results.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXDiagnosticSeverity"; }
};

struct Entity_CXDiagnosticSeverity
    : public Bind_CXDiagnosticSeverity<
          std::decay_t<pybind11::enum_<CXDiagnosticSeverity>>> {

  explicit Entity_CXDiagnosticSeverity(EntityScope parent_h)
      : Bind_CXDiagnosticSeverity<
            std::decay_t<pybind11::enum_<CXDiagnosticSeverity>>>(parent_h),
        handle{parent_h, "CXDiagnosticSeverity", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the severity of a particular diagnostic.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXDiagnosticSeverity> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXDiagnosticSeverity

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNumDiagnosticsInSet
template <class Pybind11T>
struct Bind_clang_getNumDiagnosticsInSet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNumDiagnosticsInSet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getNumDiagnosticsInSet",
        [=](pybind11_weaver::PointerWrapper<void *> *Diags) {
          return clang_getNumDiagnosticsInSet((Diags)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine the number of diagnostics in a CXDiagnosticSet.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNumDiagnosticsInSet"; }
};

struct Entity_clang_getNumDiagnosticsInSet
    : public Bind_clang_getNumDiagnosticsInSet<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNumDiagnosticsInSet(EntityScope parent_h)
      : Bind_clang_getNumDiagnosticsInSet<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNumDiagnosticsInSet

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticInSet
template <class Pybind11T>
struct Bind_clang_getDiagnosticInSet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticInSet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticInSet",
        [=](pybind11_weaver::PointerWrapper<void *> *Diags,
            unsigned int Index) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getDiagnosticInSet((Diags)->ptr, Index))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve a diagnostic associated with the given CXDiagnosticSet.
 *
 * \param Diags the CXDiagnosticSet to query.
 * \param Index the zero-based diagnostic number to retrieve.
 *
 * \returns the requested diagnostic. This diagnostic must be freed
 * via a call to \c clang_disposeDiagnostic().
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticInSet"; }
};

struct Entity_clang_getDiagnosticInSet
    : public Bind_clang_getDiagnosticInSet<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticInSet(EntityScope parent_h)
      : Bind_clang_getDiagnosticInSet<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticInSet

#ifndef PB11_WEAVER_DISABLE_Entity_CXLoadDiag_Error
template <class Pybind11T> struct Bind_CXLoadDiag_Error : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXLoadDiag_Error(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXLoadDiag_None", CXLoadDiag_Error::CXLoadDiag_None,
                   R"_pb11_weaver(/**
   * Indicates that no error occurred.
   */)_pb11_weaver");
    pb11_obj.value("CXLoadDiag_Unknown", CXLoadDiag_Error::CXLoadDiag_Unknown,
                   R"_pb11_weaver(/**
   * Indicates that an unknown error occurred while attempting to
   * deserialize diagnostics.
   */)_pb11_weaver");
    pb11_obj.value("CXLoadDiag_CannotLoad",
                   CXLoadDiag_Error::CXLoadDiag_CannotLoad, R"_pb11_weaver(/**
   * Indicates that the file containing the serialized diagnostics
   * could not be opened.
   */)_pb11_weaver");
    pb11_obj.value("CXLoadDiag_InvalidFile",
                   CXLoadDiag_Error::CXLoadDiag_InvalidFile, R"_pb11_weaver(/**
   * Indicates that the serialized diagnostics file is invalid or
   * corrupt.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXLoadDiag_Error"; }
};

struct Entity_CXLoadDiag_Error
    : public Bind_CXLoadDiag_Error<
          std::decay_t<pybind11::enum_<CXLoadDiag_Error>>> {

  explicit Entity_CXLoadDiag_Error(EntityScope parent_h)
      : Bind_CXLoadDiag_Error<std::decay_t<pybind11::enum_<CXLoadDiag_Error>>>(
            parent_h),
        handle{parent_h, "CXLoadDiag_Error", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the kind of error that occurred (if any) in a call to
 * \c clang_loadDiagnostics.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXLoadDiag_Error> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXLoadDiag_Error

#ifndef PB11_WEAVER_DISABLE_Entity_clang_loadDiagnostics
template <class Pybind11T>
struct Bind_clang_loadDiagnostics : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_loadDiagnostics(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_loadDiagnostics",
        [=](const char *file, CXLoadDiag_Error *error, CXString *errorString) {
          return pybind11_weaver::WrapP<void *>(
                     clang_loadDiagnostics(file, error, errorString))
              .release();
        },
        R"_pb11_weaver(/**
 * Deserialize a set of diagnostics from a Clang diagnostics bitcode
 * file.
 *
 * \param file The name of the file to deserialize.
 * \param error A pointer to a enum value recording if there was a problem
 *        deserializing the diagnostics.
 * \param errorString A pointer to a CXString for recording the error string
 *        if the file was not successfully loaded.
 *
 * \returns A loaded CXDiagnosticSet if successful, and NULL otherwise.  These
 * diagnostics should be released using clang_disposeDiagnosticSet().
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_loadDiagnostics"; }
};

struct Entity_clang_loadDiagnostics
    : public Bind_clang_loadDiagnostics<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_loadDiagnostics(EntityScope parent_h)
      : Bind_clang_loadDiagnostics<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_loadDiagnostics

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeDiagnosticSet
template <class Pybind11T>
struct Bind_clang_disposeDiagnosticSet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeDiagnosticSet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_disposeDiagnosticSet",
        [=](pybind11_weaver::PointerWrapper<void *> *Diags) {
          return clang_disposeDiagnosticSet((Diags)->ptr);
        },
        R"_pb11_weaver(/**
 * Release a CXDiagnosticSet and all of its contained diagnostics.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeDiagnosticSet"; }
};

struct Entity_clang_disposeDiagnosticSet
    : public Bind_clang_disposeDiagnosticSet<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeDiagnosticSet(EntityScope parent_h)
      : Bind_clang_disposeDiagnosticSet<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeDiagnosticSet

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getChildDiagnostics
template <class Pybind11T>
struct Bind_clang_getChildDiagnostics : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getChildDiagnostics(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getChildDiagnostics",
        [=](pybind11_weaver::PointerWrapper<void *> *D) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getChildDiagnostics((D)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve the child diagnostics of a CXDiagnostic.
 *
 * This CXDiagnosticSet does not need to be released by
 * clang_disposeDiagnosticSet.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getChildDiagnostics"; }
};

struct Entity_clang_getChildDiagnostics
    : public Bind_clang_getChildDiagnostics<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getChildDiagnostics(EntityScope parent_h)
      : Bind_clang_getChildDiagnostics<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getChildDiagnostics

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeDiagnostic
template <class Pybind11T>
struct Bind_clang_disposeDiagnostic : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeDiagnostic(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_disposeDiagnostic",
        [=](pybind11_weaver::PointerWrapper<void *> *Diagnostic) {
          return clang_disposeDiagnostic((Diagnostic)->ptr);
        },
        R"_pb11_weaver(/**
 * Destroy a diagnostic.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeDiagnostic"; }
};

struct Entity_clang_disposeDiagnostic
    : public Bind_clang_disposeDiagnostic<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeDiagnostic(EntityScope parent_h)
      : Bind_clang_disposeDiagnostic<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeDiagnostic

#ifndef PB11_WEAVER_DISABLE_Entity_CXDiagnosticDisplayOptions
template <class Pybind11T>
struct Bind_CXDiagnosticDisplayOptions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXDiagnosticDisplayOptions(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value(
        "CXDiagnostic_DisplaySourceLocation",
        CXDiagnosticDisplayOptions::CXDiagnostic_DisplaySourceLocation,
        R"_pb11_weaver(/**
   * Display the source-location information where the
   * diagnostic was located.
   *
   * When set, diagnostics will be prefixed by the file, line, and
   * (optionally) column to which the diagnostic refers. For example,
   *
   * \code
   * test.c:28: warning: extra tokens at end of #endif directive
   * \endcode
   *
   * This option corresponds to the clang flag \c -fshow-source-location.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_DisplayColumn",
                   CXDiagnosticDisplayOptions::CXDiagnostic_DisplayColumn,
                   R"_pb11_weaver(/**
   * If displaying the source-location information of the
   * diagnostic, also include the column number.
   *
   * This option corresponds to the clang flag \c -fshow-column.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_DisplaySourceRanges",
                   CXDiagnosticDisplayOptions::CXDiagnostic_DisplaySourceRanges,
                   R"_pb11_weaver(/**
   * If displaying the source-location information of the
   * diagnostic, also include information about source ranges in a
   * machine-parsable format.
   *
   * This option corresponds to the clang flag
   * \c -fdiagnostics-print-source-range-info.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_DisplayOption",
                   CXDiagnosticDisplayOptions::CXDiagnostic_DisplayOption,
                   R"_pb11_weaver(/**
   * Display the option name associated with this diagnostic, if any.
   *
   * The option name displayed (e.g., -Wconversion) will be placed in brackets
   * after the diagnostic text. This option corresponds to the clang flag
   * \c -fdiagnostics-show-option.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_DisplayCategoryId",
                   CXDiagnosticDisplayOptions::CXDiagnostic_DisplayCategoryId,
                   R"_pb11_weaver(/**
   * Display the category number associated with this diagnostic, if any.
   *
   * The category number is displayed within brackets after the diagnostic text.
   * This option corresponds to the clang flag
   * \c -fdiagnostics-show-category=id.
   */)_pb11_weaver");
    pb11_obj.value("CXDiagnostic_DisplayCategoryName",
                   CXDiagnosticDisplayOptions::CXDiagnostic_DisplayCategoryName,
                   R"_pb11_weaver(/**
   * Display the category name associated with this diagnostic, if any.
   *
   * The category name is displayed within brackets after the diagnostic text.
   * This option corresponds to the clang flag
   * \c -fdiagnostics-show-category=name.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXDiagnosticDisplayOptions"; }
};

struct Entity_CXDiagnosticDisplayOptions
    : public Bind_CXDiagnosticDisplayOptions<
          std::decay_t<pybind11::enum_<CXDiagnosticDisplayOptions>>> {

  explicit Entity_CXDiagnosticDisplayOptions(EntityScope parent_h)
      : Bind_CXDiagnosticDisplayOptions<
            std::decay_t<pybind11::enum_<CXDiagnosticDisplayOptions>>>(
            parent_h),
        handle{parent_h, "CXDiagnosticDisplayOptions", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Options to control the display of diagnostics.
 *
 * The values in this enum are meant to be combined to customize the
 * behavior of \c clang_formatDiagnostic().
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXDiagnosticDisplayOptions> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXDiagnosticDisplayOptions

#ifndef PB11_WEAVER_DISABLE_Entity_clang_formatDiagnostic
template <class Pybind11T>
struct Bind_clang_formatDiagnostic : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_formatDiagnostic(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_formatDiagnostic",
        [=](pybind11_weaver::PointerWrapper<void *> *Diagnostic,
            unsigned int Options) {
          return clang_formatDiagnostic((Diagnostic)->ptr, Options);
        },
        R"_pb11_weaver(/**
 * Format the given diagnostic in a manner that is suitable for display.
 *
 * This routine will format the given diagnostic to a string, rendering
 * the diagnostic according to the various options given. The
 * \c clang_defaultDiagnosticDisplayOptions() function returns the set of
 * options that most closely mimics the behavior of the clang compiler.
 *
 * \param Diagnostic The diagnostic to print.
 *
 * \param Options A set of options that control the diagnostic display,
 * created by combining \c CXDiagnosticDisplayOptions values.
 *
 * \returns A new string containing for formatted diagnostic.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_formatDiagnostic"; }
};

struct Entity_clang_formatDiagnostic
    : public Bind_clang_formatDiagnostic<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_formatDiagnostic(EntityScope parent_h)
      : Bind_clang_formatDiagnostic<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_formatDiagnostic

#ifndef PB11_WEAVER_DISABLE_Entity_clang_defaultDiagnosticDisplayOptions
template <class Pybind11T>
struct Bind_clang_defaultDiagnosticDisplayOptions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_defaultDiagnosticDisplayOptions(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_defaultDiagnosticDisplayOptions",
        static_cast<unsigned int (*)()>(&clang_defaultDiagnosticDisplayOptions),
        R"_pb11_weaver(/**
 * Retrieve the set of display options most similar to the
 * default behavior of the clang compiler.
 *
 * \returns A set of display options suitable for use with \c
 * clang_formatDiagnostic().
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_defaultDiagnosticDisplayOptions"; }
};

struct Entity_clang_defaultDiagnosticDisplayOptions
    : public Bind_clang_defaultDiagnosticDisplayOptions<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_defaultDiagnosticDisplayOptions(EntityScope parent_h)
      : Bind_clang_defaultDiagnosticDisplayOptions<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_defaultDiagnosticDisplayOptions

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticSeverity
template <class Pybind11T>
struct Bind_clang_getDiagnosticSeverity : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticSeverity(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticSeverity",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_getDiagnosticSeverity((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine the severity of the given diagnostic.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticSeverity"; }
};

struct Entity_clang_getDiagnosticSeverity
    : public Bind_clang_getDiagnosticSeverity<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticSeverity(EntityScope parent_h)
      : Bind_clang_getDiagnosticSeverity<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticSeverity

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticLocation
template <class Pybind11T>
struct Bind_clang_getDiagnosticLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticLocation",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_getDiagnosticLocation((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the source location of the given diagnostic.
 *
 * This location is where Clang would print the caret ('^') when
 * displaying the diagnostic on the command line.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticLocation"; }
};

struct Entity_clang_getDiagnosticLocation
    : public Bind_clang_getDiagnosticLocation<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticLocation(EntityScope parent_h)
      : Bind_clang_getDiagnosticLocation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticSpelling
template <class Pybind11T>
struct Bind_clang_getDiagnosticSpelling : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticSpelling(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticSpelling",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_getDiagnosticSpelling((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the text of the given diagnostic.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticSpelling"; }
};

struct Entity_clang_getDiagnosticSpelling
    : public Bind_clang_getDiagnosticSpelling<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticSpelling(EntityScope parent_h)
      : Bind_clang_getDiagnosticSpelling<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticSpelling

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticOption
template <class Pybind11T>
struct Bind_clang_getDiagnosticOption : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticOption(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticOption",
        [=](pybind11_weaver::PointerWrapper<void *> *Diag, CXString *Disable) {
          return clang_getDiagnosticOption((Diag)->ptr, Disable);
        },
        R"_pb11_weaver(/**
 * Retrieve the name of the command-line option that enabled this
 * diagnostic.
 *
 * \param Diag The diagnostic to be queried.
 *
 * \param Disable If non-NULL, will be set to the option that disables this
 * diagnostic (if any).
 *
 * \returns A string that contains the command-line option used to enable this
 * warning, such as "-Wconversion" or "-pedantic".
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticOption"; }
};

struct Entity_clang_getDiagnosticOption
    : public Bind_clang_getDiagnosticOption<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticOption(EntityScope parent_h)
      : Bind_clang_getDiagnosticOption<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticOption

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticCategory
template <class Pybind11T>
struct Bind_clang_getDiagnosticCategory : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticCategory(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticCategory",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_getDiagnosticCategory((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the category number for this diagnostic.
 *
 * Diagnostics can be categorized into groups along with other, related
 * diagnostics (e.g., diagnostics under the same warning flag). This routine
 * retrieves the category number for the given diagnostic.
 *
 * \returns The number of the category that contains this diagnostic, or zero
 * if this diagnostic is uncategorized.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticCategory"; }
};

struct Entity_clang_getDiagnosticCategory
    : public Bind_clang_getDiagnosticCategory<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticCategory(EntityScope parent_h)
      : Bind_clang_getDiagnosticCategory<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticCategory

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticCategoryName
template <class Pybind11T>
struct Bind_clang_getDiagnosticCategoryName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticCategoryName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getDiagnosticCategoryName",
                 static_cast<CXString (*)(unsigned int)>(
                     &clang_getDiagnosticCategoryName),
                 R"_pb11_weaver(/**
 * Retrieve the name of a particular diagnostic category.  This
 *  is now deprecated.  Use clang_getDiagnosticCategoryText()
 *  instead.
 *
 * \param Category A diagnostic category number, as returned by
 * \c clang_getDiagnosticCategory().
 *
 * \returns The name of the given diagnostic category.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticCategoryName"; }
};

struct Entity_clang_getDiagnosticCategoryName
    : public Bind_clang_getDiagnosticCategoryName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticCategoryName(EntityScope parent_h)
      : Bind_clang_getDiagnosticCategoryName<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticCategoryName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticCategoryText
template <class Pybind11T>
struct Bind_clang_getDiagnosticCategoryText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticCategoryText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticCategoryText",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_getDiagnosticCategoryText((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the diagnostic category text for a given diagnostic.
 *
 * \returns The text of the given diagnostic category.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticCategoryText"; }
};

struct Entity_clang_getDiagnosticCategoryText
    : public Bind_clang_getDiagnosticCategoryText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticCategoryText(EntityScope parent_h)
      : Bind_clang_getDiagnosticCategoryText<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticCategoryText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticNumRanges
template <class Pybind11T>
struct Bind_clang_getDiagnosticNumRanges : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticNumRanges(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticNumRanges",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_getDiagnosticNumRanges((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine the number of source ranges associated with the given
 * diagnostic.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticNumRanges"; }
};

struct Entity_clang_getDiagnosticNumRanges
    : public Bind_clang_getDiagnosticNumRanges<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticNumRanges(EntityScope parent_h)
      : Bind_clang_getDiagnosticNumRanges<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticNumRanges

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticRange
template <class Pybind11T>
struct Bind_clang_getDiagnosticRange : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticRange(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticRange",
        [=](pybind11_weaver::PointerWrapper<void *> *Diagnostic,
            unsigned int Range) {
          return clang_getDiagnosticRange((Diagnostic)->ptr, Range);
        },
        R"_pb11_weaver(/**
 * Retrieve a source range associated with the diagnostic.
 *
 * A diagnostic's source ranges highlight important elements in the source
 * code. On the command line, Clang displays source ranges by
 * underlining them with '~' characters.
 *
 * \param Diagnostic the diagnostic whose range is being extracted.
 *
 * \param Range the zero-based index specifying which range to
 *
 * \returns the requested source range.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticRange"; }
};

struct Entity_clang_getDiagnosticRange
    : public Bind_clang_getDiagnosticRange<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticRange(EntityScope parent_h)
      : Bind_clang_getDiagnosticRange<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticRange

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticNumFixIts
template <class Pybind11T>
struct Bind_clang_getDiagnosticNumFixIts : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticNumFixIts(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticNumFixIts",
        [=](pybind11_weaver::PointerWrapper<void *> *Diagnostic) {
          return clang_getDiagnosticNumFixIts((Diagnostic)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine the number of fix-it hints associated with the
 * given diagnostic.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticNumFixIts"; }
};

struct Entity_clang_getDiagnosticNumFixIts
    : public Bind_clang_getDiagnosticNumFixIts<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticNumFixIts(EntityScope parent_h)
      : Bind_clang_getDiagnosticNumFixIts<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticNumFixIts

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticFixIt
template <class Pybind11T>
struct Bind_clang_getDiagnosticFixIt : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticFixIt(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticFixIt",
        [=](pybind11_weaver::PointerWrapper<void *> *Diagnostic,
            unsigned int FixIt, CXSourceRange *ReplacementRange) {
          return clang_getDiagnosticFixIt((Diagnostic)->ptr, FixIt,
                                          ReplacementRange);
        },
        R"_pb11_weaver(/**
 * Retrieve the replacement information for a given fix-it.
 *
 * Fix-its are described in terms of a source range whose contents
 * should be replaced by a string. This approach generalizes over
 * three kinds of operations: removal of source code (the range covers
 * the code to be removed and the replacement string is empty),
 * replacement of source code (the range covers the code to be
 * replaced and the replacement string provides the new code), and
 * insertion (both the start and end of the range point at the
 * insertion location, and the replacement string provides the text to
 * insert).
 *
 * \param Diagnostic The diagnostic whose fix-its are being queried.
 *
 * \param FixIt The zero-based index of the fix-it.
 *
 * \param ReplacementRange The source range whose contents will be
 * replaced with the returned replacement string. Note that source
 * ranges are half-open ranges [a, b), so the source code should be
 * replaced from a and up to (but not including) b.
 *
 * \returns A string containing text that should be replace the source
 * code indicated by the \c ReplacementRange.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticFixIt"; }
};

struct Entity_clang_getDiagnosticFixIt
    : public Bind_clang_getDiagnosticFixIt<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticFixIt(EntityScope parent_h)
      : Bind_clang_getDiagnosticFixIt<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticFixIt

#ifndef PB11_WEAVER_DISABLE_Entity_CXUnsavedFile
template <class Pybind11T> struct Bind_CXUnsavedFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXUnsavedFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * The length of the unsaved contents of this buffer.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("Filename", &CXUnsavedFile::Filename,
                           R"_pb11_weaver(/**
   * The file whose contents have not yet been saved.
   *
   * This file must already exist in the file system.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Contents", &CXUnsavedFile::Contents,
                           R"_pb11_weaver(/**
   * A buffer containing the unsaved contents of this file.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Length", &CXUnsavedFile::Length, R"_pb11_weaver(/**
   * The length of the unsaved contents of this buffer.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXUnsavedFile"; }
};

struct Entity_CXUnsavedFile
    : public Bind_CXUnsavedFile<std::decay_t<pybind11::class_<CXUnsavedFile>>> {

  explicit Entity_CXUnsavedFile(EntityScope parent_h)
      : Bind_CXUnsavedFile<std::decay_t<pybind11::class_<CXUnsavedFile>>>(
            parent_h),
        handle{parent_h, "CXUnsavedFile", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Provides the contents of a file that has not yet been saved to disk.
 *
 * Each CXUnsavedFile instance provides the name of a file on the
 * system along with the current contents of that file that have not
 * yet been saved to disk.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXUnsavedFile> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXUnsavedFile

#ifndef PB11_WEAVER_DISABLE_Entity_CXAvailabilityKind
template <class Pybind11T> struct Bind_CXAvailabilityKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXAvailabilityKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXAvailability_Available",
                   CXAvailabilityKind::CXAvailability_Available,
                   R"_pb11_weaver(/**
   * The entity is available.
   */)_pb11_weaver");
    pb11_obj.value("CXAvailability_Deprecated",
                   CXAvailabilityKind::CXAvailability_Deprecated,
                   R"_pb11_weaver(/**
   * The entity is available, but has been deprecated (and its use is
   * not recommended).
   */)_pb11_weaver");
    pb11_obj.value("CXAvailability_NotAvailable",
                   CXAvailabilityKind::CXAvailability_NotAvailable,
                   R"_pb11_weaver(/**
   * The entity is not available; any use of it will be an error.
   */)_pb11_weaver");
    pb11_obj.value("CXAvailability_NotAccessible",
                   CXAvailabilityKind::CXAvailability_NotAccessible,
                   R"_pb11_weaver(/**
   * The entity is available, but not accessible; any use of it will be
   * an error.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXAvailabilityKind"; }
};

struct Entity_CXAvailabilityKind
    : public Bind_CXAvailabilityKind<
          std::decay_t<pybind11::enum_<CXAvailabilityKind>>> {

  explicit Entity_CXAvailabilityKind(EntityScope parent_h)
      : Bind_CXAvailabilityKind<
            std::decay_t<pybind11::enum_<CXAvailabilityKind>>>(parent_h),
        handle{parent_h, "CXAvailabilityKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the availability of a particular entity, which indicates
 * whether the use of this entity will result in a warning or error due to
 * it being deprecated or unavailable.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXAvailabilityKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXAvailabilityKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXVersion
template <class Pybind11T> struct Bind_CXVersion : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXVersion(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * The subminor version number, e.g., the '3' in '10.7.3'. This value
   * will be negative if no minor or subminor version number was provided,
   * e.g., in version '10' or '10.7'.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("Major", &CXVersion::Major, R"_pb11_weaver(/**
   * The major version number, e.g., the '10' in '10.7.3'. A negative
   * value indicates that there is no version number at all.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Minor", &CXVersion::Minor, R"_pb11_weaver(/**
   * The minor version number, e.g., the '7' in '10.7.3'. This value
   * will be negative if no minor version number was provided, e.g., for
   * version '10'.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Subminor", &CXVersion::Subminor, R"_pb11_weaver(/**
   * The subminor version number, e.g., the '3' in '10.7.3'. This value
   * will be negative if no minor or subminor version number was provided,
   * e.g., in version '10' or '10.7'.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXVersion"; }
};

struct Entity_CXVersion
    : public Bind_CXVersion<std::decay_t<pybind11::class_<CXVersion>>> {

  explicit Entity_CXVersion(EntityScope parent_h)
      : Bind_CXVersion<std::decay_t<pybind11::class_<CXVersion>>>(parent_h),
        handle{parent_h, "CXVersion", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Describes a version number of the form major.minor.subminor.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXVersion> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXVersion

#ifndef PB11_WEAVER_DISABLE_Entity_CXCursor_ExceptionSpecificationKind
template <class Pybind11T>
struct Bind_CXCursor_ExceptionSpecificationKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCursor_ExceptionSpecificationKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_None",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_None,
                   R"_pb11_weaver(/**
   * The cursor has no exception specification.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_DynamicNone",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_DynamicNone,
                   R"_pb11_weaver(/**
   * The cursor has exception specification throw()
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_Dynamic",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_Dynamic,
                   R"_pb11_weaver(/**
   * The cursor has exception specification throw(T1, T2)
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_MSAny",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_MSAny,
                   R"_pb11_weaver(/**
   * The cursor has exception specification throw(...).
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_BasicNoexcept",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_BasicNoexcept,
                   R"_pb11_weaver(/**
   * The cursor has exception specification basic noexcept.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_ComputedNoexcept",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_ComputedNoexcept,
                   R"_pb11_weaver(/**
   * The cursor has exception specification computed noexcept.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_Unevaluated",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_Unevaluated,
                   R"_pb11_weaver(/**
   * The exception specification has not yet been evaluated.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_Uninstantiated",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_Uninstantiated,
                   R"_pb11_weaver(/**
   * The exception specification has not yet been instantiated.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_Unparsed",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_Unparsed,
                   R"_pb11_weaver(/**
   * The exception specification has not been parsed yet.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ExceptionSpecificationKind_NoThrow",
                   CXCursor_ExceptionSpecificationKind::
                       CXCursor_ExceptionSpecificationKind_NoThrow,
                   R"_pb11_weaver(/**
   * The cursor has a __declspec(nothrow) exception specification.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCursor_ExceptionSpecificationKind"; }
};

struct Entity_CXCursor_ExceptionSpecificationKind
    : public Bind_CXCursor_ExceptionSpecificationKind<
          std::decay_t<pybind11::enum_<CXCursor_ExceptionSpecificationKind>>> {

  explicit Entity_CXCursor_ExceptionSpecificationKind(EntityScope parent_h)
      : Bind_CXCursor_ExceptionSpecificationKind<
            std::decay_t<pybind11::enum_<CXCursor_ExceptionSpecificationKind>>>(
            parent_h),
        handle{parent_h, "CXCursor_ExceptionSpecificationKind",
               pybind11::arithmetic(), R"_pb11_weaver(/**
 * Describes the exception specification of a cursor.
 *
 * A negative value indicates that the cursor is not a function declaration.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCursor_ExceptionSpecificationKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCursor_ExceptionSpecificationKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_createIndex
template <class Pybind11T> struct Bind_clang_createIndex : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_createIndex(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_createIndex",
        [=](int excludeDeclarationsFromPCH, int displayDiagnostics) {
          return pybind11_weaver::WrapP<void *>(
                     clang_createIndex(excludeDeclarationsFromPCH,
                                       displayDiagnostics))
              .release();
        },
        R"_pb11_weaver(/**
 * Provides a shared context for creating translation units.
 *
 * It provides two options:
 *
 * - excludeDeclarationsFromPCH: When non-zero, allows enumeration of "local"
 * declarations (when loading any new translation units). A "local" declaration
 * is one that belongs in the translation unit itself and not in a precompiled
 * header that was used by the translation unit. If zero, all declarations
 * will be enumerated.
 *
 * Here is an example:
 *
 * \code
 *   // excludeDeclsFromPCH = 1, displayDiagnostics=1
 *   Idx = clang_createIndex(1, 1);
 *
 *   // IndexTest.pch was produced with the following command:
 *   // "clang -x c IndexTest.h -emit-ast -o IndexTest.pch"
 *   TU = clang_createTranslationUnit(Idx, "IndexTest.pch");
 *
 *   // This will load all the symbols from 'IndexTest.pch'
 *   clang_visitChildren(clang_getTranslationUnitCursor(TU),
 *                       TranslationUnitVisitor, 0);
 *   clang_disposeTranslationUnit(TU);
 *
 *   // This will load all the symbols from 'IndexTest.c', excluding symbols
 *   // from 'IndexTest.pch'.
 *   char *args[] = { "-Xclang", "-include-pch=IndexTest.pch" };
 *   TU = clang_createTranslationUnitFromSourceFile(Idx, "IndexTest.c", 2, args,
 *                                                  0, 0);
 *   clang_visitChildren(clang_getTranslationUnitCursor(TU),
 *                       TranslationUnitVisitor, 0);
 *   clang_disposeTranslationUnit(TU);
 * \endcode
 *
 * This process of creating the 'pch', loading it separately, and using it (via
 * -include-pch) allows 'excludeDeclsFromPCH' to remove redundant callbacks
 * (which gives the indexer the same performance benefit as the compiler).
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_createIndex"; }
};

struct Entity_clang_createIndex
    : public Bind_clang_createIndex<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_createIndex(EntityScope parent_h)
      : Bind_clang_createIndex<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_createIndex

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeIndex
template <class Pybind11T> struct Bind_clang_disposeIndex : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeIndex(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_disposeIndex",
        [=](pybind11_weaver::PointerWrapper<void *> *index) {
          return clang_disposeIndex((index)->ptr);
        },
        R"_pb11_weaver(/**
 * Destroy the given index.
 *
 * The index must not be destroyed until all of the translation units created
 * within that index have been destroyed.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeIndex"; }
};

struct Entity_clang_disposeIndex
    : public Bind_clang_disposeIndex<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeIndex(EntityScope parent_h)
      : Bind_clang_disposeIndex<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeIndex

#ifndef PB11_WEAVER_DISABLE_Entity_CXGlobalOptFlags
template <class Pybind11T> struct Bind_CXGlobalOptFlags : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXGlobalOptFlags(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXGlobalOpt_None", CXGlobalOptFlags::CXGlobalOpt_None,
                   R"_pb11_weaver(/**
   * Used to indicate that no special CXIndex options are needed.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXGlobalOpt_ThreadBackgroundPriorityForIndexing",
        CXGlobalOptFlags::CXGlobalOpt_ThreadBackgroundPriorityForIndexing,
        R"_pb11_weaver(/**
   * Used to indicate that threads that libclang creates for indexing
   * purposes should use background priority.
   *
   * Affects #clang_indexSourceFile, #clang_indexTranslationUnit,
   * #clang_parseTranslationUnit, #clang_saveTranslationUnit.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXGlobalOpt_ThreadBackgroundPriorityForEditing",
        CXGlobalOptFlags::CXGlobalOpt_ThreadBackgroundPriorityForEditing,
        R"_pb11_weaver(/**
   * Used to indicate that threads that libclang creates for editing
   * purposes should use background priority.
   *
   * Affects #clang_reparseTranslationUnit, #clang_codeCompleteAt,
   * #clang_annotateTokens
   */)_pb11_weaver");
    pb11_obj.value("CXGlobalOpt_ThreadBackgroundPriorityForAll",
                   CXGlobalOptFlags::CXGlobalOpt_ThreadBackgroundPriorityForAll,
                   R"_pb11_weaver(/**
   * Used to indicate that all threads that libclang creates should use
   * background priority.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXGlobalOptFlags"; }
};

struct Entity_CXGlobalOptFlags
    : public Bind_CXGlobalOptFlags<
          std::decay_t<pybind11::enum_<CXGlobalOptFlags>>> {

  explicit Entity_CXGlobalOptFlags(EntityScope parent_h)
      : Bind_CXGlobalOptFlags<std::decay_t<pybind11::enum_<CXGlobalOptFlags>>>(
            parent_h),
        handle{parent_h, "CXGlobalOptFlags", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXGlobalOptFlags> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXGlobalOptFlags

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXIndex_setGlobalOptions
template <class Pybind11T>
struct Bind_clang_CXIndex_setGlobalOptions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXIndex_setGlobalOptions(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXIndex_setGlobalOptions",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0,
            unsigned int options) {
          return clang_CXIndex_setGlobalOptions((arg0)->ptr, options);
        },
        R"_pb11_weaver(/**
 * Sets general options associated with a CXIndex.
 *
 * For example:
 * \code
 * CXIndex idx = ...;
 * clang_CXIndex_setGlobalOptions(idx,
 *     clang_CXIndex_getGlobalOptions(idx) |
 *     CXGlobalOpt_ThreadBackgroundPriorityForIndexing);
 * \endcode
 *
 * \param options A bitmask of options, a bitwise OR of CXGlobalOpt_XXX flags.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXIndex_setGlobalOptions"; }
};

struct Entity_clang_CXIndex_setGlobalOptions
    : public Bind_clang_CXIndex_setGlobalOptions<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXIndex_setGlobalOptions(EntityScope parent_h)
      : Bind_clang_CXIndex_setGlobalOptions<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXIndex_setGlobalOptions

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXIndex_getGlobalOptions
template <class Pybind11T>
struct Bind_clang_CXIndex_getGlobalOptions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXIndex_getGlobalOptions(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXIndex_getGlobalOptions",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_CXIndex_getGlobalOptions((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Gets the general options associated with a CXIndex.
 *
 * \returns A bitmask of options, a bitwise OR of CXGlobalOpt_XXX flags that
 * are associated with the given CXIndex object.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXIndex_getGlobalOptions"; }
};

struct Entity_clang_CXIndex_getGlobalOptions
    : public Bind_clang_CXIndex_getGlobalOptions<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXIndex_getGlobalOptions(EntityScope parent_h)
      : Bind_clang_CXIndex_getGlobalOptions<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXIndex_getGlobalOptions

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXIndex_setInvocationEmissionPathOption
template <class Pybind11T>
struct Bind_clang_CXIndex_setInvocationEmissionPathOption : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXIndex_setInvocationEmissionPathOption(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXIndex_setInvocationEmissionPathOption",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0, const char *Path) {
          return clang_CXIndex_setInvocationEmissionPathOption((arg0)->ptr,
                                                               Path);
        },
        R"_pb11_weaver(/**
 * Sets the invocation emission path option in a CXIndex.
 *
 * The invocation emission path specifies a path which will contain log
 * files for certain libclang invocations. A null value (default) implies that
 * libclang invocations are not logged..
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CXIndex_setInvocationEmissionPathOption";
  }
};

struct Entity_clang_CXIndex_setInvocationEmissionPathOption
    : public Bind_clang_CXIndex_setInvocationEmissionPathOption<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXIndex_setInvocationEmissionPathOption(
      EntityScope parent_h)
      : Bind_clang_CXIndex_setInvocationEmissionPathOption<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXIndex_setInvocationEmissionPathOption

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isFileMultipleIncludeGuarded
template <class Pybind11T>
struct Bind_clang_isFileMultipleIncludeGuarded : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isFileMultipleIncludeGuarded(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isFileMultipleIncludeGuarded",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu,
            pybind11_weaver::PointerWrapper<void *> *file) {
          return clang_isFileMultipleIncludeGuarded((tu)->ptr, (file)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine whether the given header is guarded against
 * multiple inclusions, either with the conventional
 * \#ifndef/\#define/\#endif macro guards or with \#pragma once.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isFileMultipleIncludeGuarded"; }
};

struct Entity_clang_isFileMultipleIncludeGuarded
    : public Bind_clang_isFileMultipleIncludeGuarded<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isFileMultipleIncludeGuarded(EntityScope parent_h)
      : Bind_clang_isFileMultipleIncludeGuarded<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isFileMultipleIncludeGuarded

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getFile
template <class Pybind11T> struct Bind_clang_getFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getFile",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu,
            const char *file_name) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getFile((tu)->ptr, file_name))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve a file handle within the given translation unit.
 *
 * \param tu the translation unit
 *
 * \param file_name the name of the file.
 *
 * \returns the file handle for the named file in the translation unit \p tu,
 * or a NULL file handle if the file was not a part of this translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getFile"; }
};

struct Entity_clang_getFile
    : public Bind_clang_getFile<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getFile(EntityScope parent_h)
      : Bind_clang_getFile<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getFile

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getFileContents
template <class Pybind11T>
struct Bind_clang_getFileContents : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getFileContents(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getFileContents",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu,
            pybind11_weaver::PointerWrapper<void *> *file,
            unsigned long *size) {
          return clang_getFileContents((tu)->ptr, (file)->ptr, size);
        },
        R"_pb11_weaver(/**
 * Retrieve the buffer associated with the given file.
 *
 * \param tu the translation unit
 *
 * \param file the file for which to retrieve the buffer.
 *
 * \param size [out] if non-NULL, will be set to the size of the buffer.
 *
 * \returns a pointer to the buffer in memory that holds the contents of
 * \p file, or a NULL pointer when the file is not loaded.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getFileContents"; }
};

struct Entity_clang_getFileContents
    : public Bind_clang_getFileContents<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getFileContents(EntityScope parent_h)
      : Bind_clang_getFileContents<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getFileContents

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getLocation
template <class Pybind11T> struct Bind_clang_getLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getLocation",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu,
            pybind11_weaver::PointerWrapper<void *> *file, unsigned int line,
            unsigned int column) {
          return clang_getLocation((tu)->ptr, (file)->ptr, line, column);
        },
        R"_pb11_weaver(/**
 * Retrieves the source location associated with a given file/line/column
 * in a particular translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getLocation"; }
};

struct Entity_clang_getLocation
    : public Bind_clang_getLocation<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getLocation(EntityScope parent_h)
      : Bind_clang_getLocation<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getLocationForOffset
template <class Pybind11T>
struct Bind_clang_getLocationForOffset : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getLocationForOffset(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getLocationForOffset",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu,
            pybind11_weaver::PointerWrapper<void *> *file,
            unsigned int offset) {
          return clang_getLocationForOffset((tu)->ptr, (file)->ptr, offset);
        },
        R"_pb11_weaver(/**
 * Retrieves the source location associated with a given character offset
 * in a particular translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getLocationForOffset"; }
};

struct Entity_clang_getLocationForOffset
    : public Bind_clang_getLocationForOffset<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getLocationForOffset(EntityScope parent_h)
      : Bind_clang_getLocationForOffset<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getLocationForOffset

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getSkippedRanges
template <class Pybind11T>
struct Bind_clang_getSkippedRanges : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getSkippedRanges(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getSkippedRanges",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu,
            pybind11_weaver::PointerWrapper<void *> *file) {
          return clang_getSkippedRanges((tu)->ptr, (file)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve all ranges that were skipped by the preprocessor.
 *
 * The preprocessor will skip lines when they are surrounded by an
 * if/ifdef/ifndef directive whose condition does not evaluate to true.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getSkippedRanges"; }
};

struct Entity_clang_getSkippedRanges
    : public Bind_clang_getSkippedRanges<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getSkippedRanges(EntityScope parent_h)
      : Bind_clang_getSkippedRanges<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getSkippedRanges

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getAllSkippedRanges
template <class Pybind11T>
struct Bind_clang_getAllSkippedRanges : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getAllSkippedRanges(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getAllSkippedRanges",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu) {
          return clang_getAllSkippedRanges((tu)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve all ranges from all files that were skipped by the
 * preprocessor.
 *
 * The preprocessor will skip lines when they are surrounded by an
 * if/ifdef/ifndef directive whose condition does not evaluate to true.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getAllSkippedRanges"; }
};

struct Entity_clang_getAllSkippedRanges
    : public Bind_clang_getAllSkippedRanges<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getAllSkippedRanges(EntityScope parent_h)
      : Bind_clang_getAllSkippedRanges<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getAllSkippedRanges

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNumDiagnostics
template <class Pybind11T>
struct Bind_clang_getNumDiagnostics : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNumDiagnostics(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getNumDiagnostics",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *Unit) {
          return clang_getNumDiagnostics((Unit)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine the number of diagnostics produced for the given
 * translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNumDiagnostics"; }
};

struct Entity_clang_getNumDiagnostics
    : public Bind_clang_getNumDiagnostics<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNumDiagnostics(EntityScope parent_h)
      : Bind_clang_getNumDiagnostics<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNumDiagnostics

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnostic
template <class Pybind11T> struct Bind_clang_getDiagnostic : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnostic(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnostic",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *Unit,
            unsigned int Index) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getDiagnostic((Unit)->ptr, Index))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve a diagnostic associated with the given translation unit.
 *
 * \param Unit the translation unit to query.
 * \param Index the zero-based diagnostic number to retrieve.
 *
 * \returns the requested diagnostic. This diagnostic must be freed
 * via a call to \c clang_disposeDiagnostic().
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnostic"; }
};

struct Entity_clang_getDiagnostic
    : public Bind_clang_getDiagnostic<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnostic(EntityScope parent_h)
      : Bind_clang_getDiagnostic<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnostic

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticSetFromTU
template <class Pybind11T>
struct Bind_clang_getDiagnosticSetFromTU : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDiagnosticSetFromTU(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDiagnosticSetFromTU",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *Unit) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getDiagnosticSetFromTU((Unit)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve the complete set of diagnostics associated with a
 *        translation unit.
 *
 * \param Unit the translation unit to query.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDiagnosticSetFromTU"; }
};

struct Entity_clang_getDiagnosticSetFromTU
    : public Bind_clang_getDiagnosticSetFromTU<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDiagnosticSetFromTU(EntityScope parent_h)
      : Bind_clang_getDiagnosticSetFromTU<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDiagnosticSetFromTU

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTranslationUnitSpelling
template <class Pybind11T>
struct Bind_clang_getTranslationUnitSpelling : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTranslationUnitSpelling(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTranslationUnitSpelling",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *CTUnit) {
          return clang_getTranslationUnitSpelling((CTUnit)->ptr);
        },
        R"_pb11_weaver(/**
 * Get the original translation unit source file name.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTranslationUnitSpelling"; }
};

struct Entity_clang_getTranslationUnitSpelling
    : public Bind_clang_getTranslationUnitSpelling<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTranslationUnitSpelling(EntityScope parent_h)
      : Bind_clang_getTranslationUnitSpelling<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTranslationUnitSpelling

#ifndef PB11_WEAVER_DISABLE_Entity_clang_createTranslationUnitFromSourceFile
template <class Pybind11T>
struct Bind_clang_createTranslationUnitFromSourceFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_createTranslationUnitFromSourceFile(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_createTranslationUnitFromSourceFile",
        [=](pybind11_weaver::PointerWrapper<void *> *CIdx,
            const char *source_filename, int num_clang_command_line_args,
            pybind11_weaver::PointerWrapper<const char *const *>
                *clang_command_line_args,
            unsigned int num_unsaved_files, CXUnsavedFile *unsaved_files) {
          return pybind11_weaver::WrapP<CXTranslationUnitImpl *>(
                     clang_createTranslationUnitFromSourceFile(
                         (CIdx)->ptr, source_filename,
                         num_clang_command_line_args,
                         (clang_command_line_args)->ptr, num_unsaved_files,
                         unsaved_files))
              .release();
        },
        R"_pb11_weaver(/**
 * Return the CXTranslationUnit for a given source file and the provided
 * command line arguments one would pass to the compiler.
 *
 * Note: The 'source_filename' argument is optional.  If the caller provides a
 * NULL pointer, the name of the source file is expected to reside in the
 * specified command line arguments.
 *
 * Note: When encountered in 'clang_command_line_args', the following options
 * are ignored:
 *
 *   '-c'
 *   '-emit-ast'
 *   '-fsyntax-only'
 *   '-o \<output file>'  (both '-o' and '\<output file>' are ignored)
 *
 * \param CIdx The index object with which the translation unit will be
 * associated.
 *
 * \param source_filename The name of the source file to load, or NULL if the
 * source file is included in \p clang_command_line_args.
 *
 * \param num_clang_command_line_args The number of command-line arguments in
 * \p clang_command_line_args.
 *
 * \param clang_command_line_args The command-line arguments that would be
 * passed to the \c clang executable if it were being invoked out-of-process.
 * These command-line options will be parsed and will affect how the translation
 * unit is parsed. Note that the following options are ignored: '-c',
 * '-emit-ast', '-fsyntax-only' (which is the default), and '-o \<output file>'.
 *
 * \param num_unsaved_files the number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param unsaved_files the files that have not yet been saved to disk
 * but may be required for code completion, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_createTranslationUnitFromSourceFile";
  }
};

struct Entity_clang_createTranslationUnitFromSourceFile
    : public Bind_clang_createTranslationUnitFromSourceFile<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_createTranslationUnitFromSourceFile(
      EntityScope parent_h)
      : Bind_clang_createTranslationUnitFromSourceFile<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_createTranslationUnitFromSourceFile

#ifndef PB11_WEAVER_DISABLE_Entity_clang_createTranslationUnit
template <class Pybind11T>
struct Bind_clang_createTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_createTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_createTranslationUnit",
        [=](pybind11_weaver::PointerWrapper<void *> *CIdx,
            const char *ast_filename) {
          return pybind11_weaver::WrapP<CXTranslationUnitImpl *>(
                     clang_createTranslationUnit((CIdx)->ptr, ast_filename))
              .release();
        },
        R"_pb11_weaver(/**
 * Same as \c clang_createTranslationUnit2, but returns
 * the \c CXTranslationUnit instead of an error code.  In case of an error this
 * routine returns a \c NULL \c CXTranslationUnit, without further detailed
 * error codes.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_createTranslationUnit"; }
};

struct Entity_clang_createTranslationUnit
    : public Bind_clang_createTranslationUnit<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_createTranslationUnit(EntityScope parent_h)
      : Bind_clang_createTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_createTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_clang_createTranslationUnit2
template <class Pybind11T>
struct Bind_clang_createTranslationUnit2 : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_createTranslationUnit2(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_createTranslationUnit2",
        [=](pybind11_weaver::PointerWrapper<void *> *CIdx,
            const char *ast_filename,
            pybind11_weaver::PointerWrapper<CXTranslationUnitImpl **> *out_TU) {
          return clang_createTranslationUnit2((CIdx)->ptr, ast_filename,
                                              (out_TU)->ptr);
        },
        R"_pb11_weaver(/**
 * Create a translation unit from an AST file (\c -emit-ast).
 *
 * \param[out] out_TU A non-NULL pointer to store the created
 * \c CXTranslationUnit.
 *
 * \returns Zero on success, otherwise returns an error code.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_createTranslationUnit2"; }
};

struct Entity_clang_createTranslationUnit2
    : public Bind_clang_createTranslationUnit2<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_createTranslationUnit2(EntityScope parent_h)
      : Bind_clang_createTranslationUnit2<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_createTranslationUnit2

#ifndef PB11_WEAVER_DISABLE_Entity_CXTranslationUnit_Flags
template <class Pybind11T>
struct Bind_CXTranslationUnit_Flags : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTranslationUnit_Flags(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXTranslationUnit_None",
                   CXTranslationUnit_Flags::CXTranslationUnit_None,
                   R"_pb11_weaver(/**
   * Used to indicate that no special translation-unit options are
   * needed.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXTranslationUnit_DetailedPreprocessingRecord",
        CXTranslationUnit_Flags::CXTranslationUnit_DetailedPreprocessingRecord,
        R"_pb11_weaver(/**
   * Used to indicate that the parser should construct a "detailed"
   * preprocessing record, including all macro definitions and instantiations.
   *
   * Constructing a detailed preprocessing record requires more memory
   * and time to parse, since the information contained in the record
   * is usually not retained. However, it can be useful for
   * applications that require more detailed information about the
   * behavior of the preprocessor.
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_Incomplete",
                   CXTranslationUnit_Flags::CXTranslationUnit_Incomplete,
                   R"_pb11_weaver(/**
   * Used to indicate that the translation unit is incomplete.
   *
   * When a translation unit is considered "incomplete", semantic
   * analysis that is typically performed at the end of the
   * translation unit will be suppressed. For example, this suppresses
   * the completion of tentative declarations in C and of
   * instantiation of implicitly-instantiation function templates in
   * C++. This option is typically used when parsing a header with the
   * intent of producing a precompiled header.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXTranslationUnit_PrecompiledPreamble",
        CXTranslationUnit_Flags::CXTranslationUnit_PrecompiledPreamble,
        R"_pb11_weaver(/**
   * Used to indicate that the translation unit should be built with an
   * implicit precompiled header for the preamble.
   *
   * An implicit precompiled header is used as an optimization when a
   * particular translation unit is likely to be reparsed many times
   * when the sources aren't changing that often. In this case, an
   * implicit precompiled header will be built containing all of the
   * initial includes at the top of the main file (what we refer to as
   * the "preamble" of the file). In subsequent parses, if the
   * preamble or the files in it have not changed, \c
   * clang_reparseTranslationUnit() will re-use the implicit
   * precompiled header to improve parsing performance.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXTranslationUnit_CacheCompletionResults",
        CXTranslationUnit_Flags::CXTranslationUnit_CacheCompletionResults,
        R"_pb11_weaver(/**
   * Used to indicate that the translation unit should cache some
   * code-completion results with each reparse of the source file.
   *
   * Caching of code-completion results is a performance optimization that
   * introduces some overhead to reparsing but improves the performance of
   * code-completion operations.
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_ForSerialization",
                   CXTranslationUnit_Flags::CXTranslationUnit_ForSerialization,
                   R"_pb11_weaver(/**
   * Used to indicate that the translation unit will be serialized with
   * \c clang_saveTranslationUnit.
   *
   * This option is typically used when parsing a header with the intent of
   * producing a precompiled header.
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_CXXChainedPCH",
                   CXTranslationUnit_Flags::CXTranslationUnit_CXXChainedPCH,
                   R"_pb11_weaver(/**
   * DEPRECATED: Enabled chained precompiled preambles in C++.
   *
   * Note: this is a *temporary* option that is available only while
   * we are testing C++ precompiled preamble support. It is deprecated.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXTranslationUnit_SkipFunctionBodies",
        CXTranslationUnit_Flags::CXTranslationUnit_SkipFunctionBodies,
        R"_pb11_weaver(/**
   * Used to indicate that function/method bodies should be skipped while
   * parsing.
   *
   * This option can be used to search for declarations/definitions while
   * ignoring the usages.
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_IncludeBriefCommentsInCodeCompletion",
                   CXTranslationUnit_Flags::
                       CXTranslationUnit_IncludeBriefCommentsInCodeCompletion,
                   R"_pb11_weaver(/**
   * Used to indicate that brief documentation comments should be
   * included into the set of code completions returned from this translation
   * unit.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXTranslationUnit_CreatePreambleOnFirstParse",
        CXTranslationUnit_Flags::CXTranslationUnit_CreatePreambleOnFirstParse,
        R"_pb11_weaver(/**
   * Used to indicate that the precompiled preamble should be created on
   * the first parse. Otherwise it will be created on the first reparse. This
   * trades runtime on the first parse (serializing the preamble takes time) for
   * reduced runtime on the second parse (can now reuse the preamble).
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_KeepGoing",
                   CXTranslationUnit_Flags::CXTranslationUnit_KeepGoing,
                   R"_pb11_weaver(/**
   * Do not stop processing when fatal errors are encountered.
   *
   * When fatal errors are encountered while parsing a translation unit,
   * semantic analysis is typically stopped early when compiling code. A common
   * source for fatal errors are unresolvable include files. For the
   * purposes of an IDE, this is undesirable behavior and as much information
   * as possible should be reported. Use this flag to enable this behavior.
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_SingleFileParse",
                   CXTranslationUnit_Flags::CXTranslationUnit_SingleFileParse,
                   R"_pb11_weaver(/**
   * Sets the preprocessor in a mode for parsing a single file only.
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_LimitSkipFunctionBodiesToPreamble",
                   CXTranslationUnit_Flags::
                       CXTranslationUnit_LimitSkipFunctionBodiesToPreamble,
                   R"_pb11_weaver(/**
   * Used in combination with CXTranslationUnit_SkipFunctionBodies to
   * constrain the skipping of function bodies to the preamble.
   *
   * The function bodies of the main file are not skipped.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXTranslationUnit_IncludeAttributedTypes",
        CXTranslationUnit_Flags::CXTranslationUnit_IncludeAttributedTypes,
        R"_pb11_weaver(/**
   * Used to indicate that attributed types should be included in CXType.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXTranslationUnit_VisitImplicitAttributes",
        CXTranslationUnit_Flags::CXTranslationUnit_VisitImplicitAttributes,
        R"_pb11_weaver(/**
   * Used to indicate that implicit attributes should be visited.
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles",
                   CXTranslationUnit_Flags::
                       CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles,
                   R"_pb11_weaver(/**
   * Used to indicate that non-errors from included files should be ignored.
   *
   * If set, clang_getDiagnosticSetFromTU() will not report e.g. warnings from
   * included files anymore. This speeds up clang_getDiagnosticSetFromTU() for
   * the case where these warnings are not of interest, as for an IDE for
   * example, which typically shows only the diagnostics in the main file.
   */)_pb11_weaver");
    pb11_obj.value("CXTranslationUnit_RetainExcludedConditionalBlocks",
                   CXTranslationUnit_Flags::
                       CXTranslationUnit_RetainExcludedConditionalBlocks,
                   R"_pb11_weaver(/**
   * Tells the preprocessor not to skip excluded conditional blocks.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXTranslationUnit_Flags"; }
};

struct Entity_CXTranslationUnit_Flags
    : public Bind_CXTranslationUnit_Flags<
          std::decay_t<pybind11::enum_<CXTranslationUnit_Flags>>> {

  explicit Entity_CXTranslationUnit_Flags(EntityScope parent_h)
      : Bind_CXTranslationUnit_Flags<
            std::decay_t<pybind11::enum_<CXTranslationUnit_Flags>>>(parent_h),
        handle{parent_h, "CXTranslationUnit_Flags", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Flags that control the creation of translation units.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * constructing the translation unit.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXTranslationUnit_Flags> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTranslationUnit_Flags

#ifndef PB11_WEAVER_DISABLE_Entity_clang_defaultEditingTranslationUnitOptions
template <class Pybind11T>
struct Bind_clang_defaultEditingTranslationUnitOptions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_defaultEditingTranslationUnitOptions(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_defaultEditingTranslationUnitOptions",
                 static_cast<unsigned int (*)()>(
                     &clang_defaultEditingTranslationUnitOptions),
                 R"_pb11_weaver(/**
 * Returns the set of flags that is suitable for parsing a translation
 * unit that is being edited.
 *
 * The set of flags returned provide options for \c clang_parseTranslationUnit()
 * to indicate that the translation unit is likely to be reparsed many times,
 * either explicitly (via \c clang_reparseTranslationUnit()) or implicitly
 * (e.g., by code completion (\c clang_codeCompletionAt())). The returned flag
 * set contains an unspecified set of optimizations (e.g., the precompiled
 * preamble) geared toward improving the performance of these routines. The
 * set of optimizations enabled may change from one version to the next.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_defaultEditingTranslationUnitOptions";
  }
};

struct Entity_clang_defaultEditingTranslationUnitOptions
    : public Bind_clang_defaultEditingTranslationUnitOptions<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_defaultEditingTranslationUnitOptions(
      EntityScope parent_h)
      : Bind_clang_defaultEditingTranslationUnitOptions<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_defaultEditingTranslationUnitOptions

#ifndef PB11_WEAVER_DISABLE_Entity_clang_parseTranslationUnit
template <class Pybind11T>
struct Bind_clang_parseTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_parseTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_parseTranslationUnit",
        [=](pybind11_weaver::PointerWrapper<void *> *CIdx,
            const char *source_filename,
            pybind11_weaver::PointerWrapper<const char *const *>
                *command_line_args,
            int num_command_line_args, CXUnsavedFile *unsaved_files,
            unsigned int num_unsaved_files, unsigned int options) {
          return pybind11_weaver::WrapP<CXTranslationUnitImpl *>(
                     clang_parseTranslationUnit(
                         (CIdx)->ptr, source_filename, (command_line_args)->ptr,
                         num_command_line_args, unsaved_files,
                         num_unsaved_files, options))
              .release();
        },
        R"_pb11_weaver(/**
 * Same as \c clang_parseTranslationUnit2, but returns
 * the \c CXTranslationUnit instead of an error code.  In case of an error this
 * routine returns a \c NULL \c CXTranslationUnit, without further detailed
 * error codes.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_parseTranslationUnit"; }
};

struct Entity_clang_parseTranslationUnit
    : public Bind_clang_parseTranslationUnit<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_parseTranslationUnit(EntityScope parent_h)
      : Bind_clang_parseTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_parseTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_clang_parseTranslationUnit2
template <class Pybind11T>
struct Bind_clang_parseTranslationUnit2 : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_parseTranslationUnit2(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_parseTranslationUnit2",
        [=](pybind11_weaver::PointerWrapper<void *> *CIdx,
            const char *source_filename,
            pybind11_weaver::PointerWrapper<const char *const *>
                *command_line_args,
            int num_command_line_args, CXUnsavedFile *unsaved_files,
            unsigned int num_unsaved_files, unsigned int options,
            pybind11_weaver::PointerWrapper<CXTranslationUnitImpl **> *out_TU) {
          return clang_parseTranslationUnit2(
              (CIdx)->ptr, source_filename, (command_line_args)->ptr,
              num_command_line_args, unsaved_files, num_unsaved_files, options,
              (out_TU)->ptr);
        },
        R"_pb11_weaver(/**
 * Parse the given source file and the translation unit corresponding
 * to that file.
 *
 * This routine is the main entry point for the Clang C API, providing the
 * ability to parse a source file into a translation unit that can then be
 * queried by other functions in the API. This routine accepts a set of
 * command-line arguments so that the compilation can be configured in the same
 * way that the compiler is configured on the command line.
 *
 * \param CIdx The index object with which the translation unit will be
 * associated.
 *
 * \param source_filename The name of the source file to load, or NULL if the
 * source file is included in \c command_line_args.
 *
 * \param command_line_args The command-line arguments that would be
 * passed to the \c clang executable if it were being invoked out-of-process.
 * These command-line options will be parsed and will affect how the translation
 * unit is parsed. Note that the following options are ignored: '-c',
 * '-emit-ast', '-fsyntax-only' (which is the default), and '-o \<output file>'.
 *
 * \param num_command_line_args The number of command-line arguments in
 * \c command_line_args.
 *
 * \param unsaved_files the files that have not yet been saved to disk
 * but may be required for parsing, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 *
 * \param num_unsaved_files the number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param options A bitmask of options that affects how the translation unit
 * is managed but not its compilation. This should be a bitwise OR of the
 * CXTranslationUnit_XXX flags.
 *
 * \param[out] out_TU A non-NULL pointer to store the created
 * \c CXTranslationUnit, describing the parsed code and containing any
 * diagnostics produced by the compiler.
 *
 * \returns Zero on success, otherwise returns an error code.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_parseTranslationUnit2"; }
};

struct Entity_clang_parseTranslationUnit2
    : public Bind_clang_parseTranslationUnit2<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_parseTranslationUnit2(EntityScope parent_h)
      : Bind_clang_parseTranslationUnit2<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_parseTranslationUnit2

#ifndef PB11_WEAVER_DISABLE_Entity_clang_parseTranslationUnit2FullArgv
template <class Pybind11T>
struct Bind_clang_parseTranslationUnit2FullArgv : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_parseTranslationUnit2FullArgv(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_parseTranslationUnit2FullArgv",
        [=](pybind11_weaver::PointerWrapper<void *> *CIdx,
            const char *source_filename,
            pybind11_weaver::PointerWrapper<const char *const *>
                *command_line_args,
            int num_command_line_args, CXUnsavedFile *unsaved_files,
            unsigned int num_unsaved_files, unsigned int options,
            pybind11_weaver::PointerWrapper<CXTranslationUnitImpl **> *out_TU) {
          return clang_parseTranslationUnit2FullArgv(
              (CIdx)->ptr, source_filename, (command_line_args)->ptr,
              num_command_line_args, unsaved_files, num_unsaved_files, options,
              (out_TU)->ptr);
        },
        R"_pb11_weaver(/**
 * Same as clang_parseTranslationUnit2 but requires a full command line
 * for \c command_line_args including argv[0]. This is useful if the standard
 * library paths are relative to the binary.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_parseTranslationUnit2FullArgv"; }
};

struct Entity_clang_parseTranslationUnit2FullArgv
    : public Bind_clang_parseTranslationUnit2FullArgv<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_parseTranslationUnit2FullArgv(EntityScope parent_h)
      : Bind_clang_parseTranslationUnit2FullArgv<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_parseTranslationUnit2FullArgv

#ifndef PB11_WEAVER_DISABLE_Entity_CXSaveTranslationUnit_Flags
template <class Pybind11T>
struct Bind_CXSaveTranslationUnit_Flags : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXSaveTranslationUnit_Flags(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXSaveTranslationUnit_None",
                   CXSaveTranslationUnit_Flags::CXSaveTranslationUnit_None,
                   R"_pb11_weaver(/**
   * Used to indicate that no special saving options are needed.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXSaveTranslationUnit_Flags"; }
};

struct Entity_CXSaveTranslationUnit_Flags
    : public Bind_CXSaveTranslationUnit_Flags<
          std::decay_t<pybind11::enum_<CXSaveTranslationUnit_Flags>>> {

  explicit Entity_CXSaveTranslationUnit_Flags(EntityScope parent_h)
      : Bind_CXSaveTranslationUnit_Flags<
            std::decay_t<pybind11::enum_<CXSaveTranslationUnit_Flags>>>(
            parent_h),
        handle{parent_h, "CXSaveTranslationUnit_Flags", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Flags that control how translation units are saved.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * saving the translation unit.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXSaveTranslationUnit_Flags> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXSaveTranslationUnit_Flags

#ifndef PB11_WEAVER_DISABLE_Entity_clang_defaultSaveOptions
template <class Pybind11T>
struct Bind_clang_defaultSaveOptions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_defaultSaveOptions(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_defaultSaveOptions",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU) {
          return clang_defaultSaveOptions((TU)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the set of flags that is suitable for saving a translation
 * unit.
 *
 * The set of flags returned provide options for
 * \c clang_saveTranslationUnit() by default. The returned flag
 * set contains an unspecified set of options that save translation units with
 * the most commonly-requested data.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_defaultSaveOptions"; }
};

struct Entity_clang_defaultSaveOptions
    : public Bind_clang_defaultSaveOptions<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_defaultSaveOptions(EntityScope parent_h)
      : Bind_clang_defaultSaveOptions<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_defaultSaveOptions

#ifndef PB11_WEAVER_DISABLE_Entity_CXSaveError
template <class Pybind11T> struct Bind_CXSaveError : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXSaveError(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXSaveError_None", CXSaveError::CXSaveError_None,
                   R"_pb11_weaver(/**
   * Indicates that no error occurred while saving a translation unit.
   */)_pb11_weaver");
    pb11_obj.value("CXSaveError_Unknown", CXSaveError::CXSaveError_Unknown,
                   R"_pb11_weaver(/**
   * Indicates that an unknown error occurred while attempting to save
   * the file.
   *
   * This error typically indicates that file I/O failed when attempting to
   * write the file.
   */)_pb11_weaver");
    pb11_obj.value("CXSaveError_TranslationErrors",
                   CXSaveError::CXSaveError_TranslationErrors,
                   R"_pb11_weaver(/**
   * Indicates that errors during translation prevented this attempt
   * to save the translation unit.
   *
   * Errors that prevent the translation unit from being saved can be
   * extracted using \c clang_getNumDiagnostics() and \c clang_getDiagnostic().
   */)_pb11_weaver");
    pb11_obj.value("CXSaveError_InvalidTU", CXSaveError::CXSaveError_InvalidTU,
                   R"_pb11_weaver(/**
   * Indicates that the translation unit to be saved was somehow
   * invalid (e.g., NULL).
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXSaveError"; }
};

struct Entity_CXSaveError
    : public Bind_CXSaveError<std::decay_t<pybind11::enum_<CXSaveError>>> {

  explicit Entity_CXSaveError(EntityScope parent_h)
      : Bind_CXSaveError<std::decay_t<pybind11::enum_<CXSaveError>>>(parent_h),
        handle{parent_h, "CXSaveError", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the kind of error that occurred (if any) in a call to
 * \c clang_saveTranslationUnit().
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXSaveError> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXSaveError

#ifndef PB11_WEAVER_DISABLE_Entity_clang_saveTranslationUnit
template <class Pybind11T>
struct Bind_clang_saveTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_saveTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_saveTranslationUnit",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU,
            const char *FileName, unsigned int options) {
          return clang_saveTranslationUnit((TU)->ptr, FileName, options);
        },
        R"_pb11_weaver(/**
 * Saves a translation unit into a serialized representation of
 * that translation unit on disk.
 *
 * Any translation unit that was parsed without error can be saved
 * into a file. The translation unit can then be deserialized into a
 * new \c CXTranslationUnit with \c clang_createTranslationUnit() or,
 * if it is an incomplete translation unit that corresponds to a
 * header, used as a precompiled header when parsing other translation
 * units.
 *
 * \param TU The translation unit to save.
 *
 * \param FileName The file to which the translation unit will be saved.
 *
 * \param options A bitmask of options that affects how the translation unit
 * is saved. This should be a bitwise OR of the
 * CXSaveTranslationUnit_XXX flags.
 *
 * \returns A value that will match one of the enumerators of the CXSaveError
 * enumeration. Zero (CXSaveError_None) indicates that the translation unit was
 * saved successfully, while a non-zero value indicates that a problem occurred.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_saveTranslationUnit"; }
};

struct Entity_clang_saveTranslationUnit
    : public Bind_clang_saveTranslationUnit<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_saveTranslationUnit(EntityScope parent_h)
      : Bind_clang_saveTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_saveTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_clang_suspendTranslationUnit
template <class Pybind11T>
struct Bind_clang_suspendTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_suspendTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_suspendTranslationUnit",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0) {
          return clang_suspendTranslationUnit((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Suspend a translation unit in order to free memory associated with it.
 *
 * A suspended translation unit uses significantly less memory but on the other
 * side does not support any other calls than \c clang_reparseTranslationUnit
 * to resume it or \c clang_disposeTranslationUnit to dispose it completely.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_suspendTranslationUnit"; }
};

struct Entity_clang_suspendTranslationUnit
    : public Bind_clang_suspendTranslationUnit<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_suspendTranslationUnit(EntityScope parent_h)
      : Bind_clang_suspendTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_suspendTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeTranslationUnit
template <class Pybind11T>
struct Bind_clang_disposeTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_disposeTranslationUnit",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0) {
          return clang_disposeTranslationUnit((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Destroy the specified CXTranslationUnit object.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeTranslationUnit"; }
};

struct Entity_clang_disposeTranslationUnit
    : public Bind_clang_disposeTranslationUnit<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeTranslationUnit(EntityScope parent_h)
      : Bind_clang_disposeTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_CXReparse_Flags
template <class Pybind11T> struct Bind_CXReparse_Flags : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXReparse_Flags(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXReparse_None", CXReparse_Flags::CXReparse_None,
                   R"_pb11_weaver(/**
   * Used to indicate that no special reparsing options are needed.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXReparse_Flags"; }
};

struct Entity_CXReparse_Flags
    : public Bind_CXReparse_Flags<
          std::decay_t<pybind11::enum_<CXReparse_Flags>>> {

  explicit Entity_CXReparse_Flags(EntityScope parent_h)
      : Bind_CXReparse_Flags<std::decay_t<pybind11::enum_<CXReparse_Flags>>>(
            parent_h),
        handle{parent_h, "CXReparse_Flags", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Flags that control the reparsing of translation units.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * reparsing the translation unit.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXReparse_Flags> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXReparse_Flags

#ifndef PB11_WEAVER_DISABLE_Entity_clang_defaultReparseOptions
template <class Pybind11T>
struct Bind_clang_defaultReparseOptions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_defaultReparseOptions(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_defaultReparseOptions",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU) {
          return clang_defaultReparseOptions((TU)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the set of flags that is suitable for reparsing a translation
 * unit.
 *
 * The set of flags returned provide options for
 * \c clang_reparseTranslationUnit() by default. The returned flag
 * set contains an unspecified set of optimizations geared toward common uses
 * of reparsing. The set of optimizations enabled may change from one version
 * to the next.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_defaultReparseOptions"; }
};

struct Entity_clang_defaultReparseOptions
    : public Bind_clang_defaultReparseOptions<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_defaultReparseOptions(EntityScope parent_h)
      : Bind_clang_defaultReparseOptions<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_defaultReparseOptions

#ifndef PB11_WEAVER_DISABLE_Entity_clang_reparseTranslationUnit
template <class Pybind11T>
struct Bind_clang_reparseTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_reparseTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_reparseTranslationUnit",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU,
            unsigned int num_unsaved_files, CXUnsavedFile *unsaved_files,
            unsigned int options) {
          return clang_reparseTranslationUnit((TU)->ptr, num_unsaved_files,
                                              unsaved_files, options);
        },
        R"_pb11_weaver(/**
 * Reparse the source files that produced this translation unit.
 *
 * This routine can be used to re-parse the source files that originally
 * created the given translation unit, for example because those source files
 * have changed (either on disk or as passed via \p unsaved_files). The
 * source code will be reparsed with the same command-line options as it
 * was originally parsed.
 *
 * Reparsing a translation unit invalidates all cursors and source locations
 * that refer into that translation unit. This makes reparsing a translation
 * unit semantically equivalent to destroying the translation unit and then
 * creating a new translation unit with the same command-line arguments.
 * However, it may be more efficient to reparse a translation
 * unit using this routine.
 *
 * \param TU The translation unit whose contents will be re-parsed. The
 * translation unit must originally have been built with
 * \c clang_createTranslationUnitFromSourceFile().
 *
 * \param num_unsaved_files The number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param unsaved_files The files that have not yet been saved to disk
 * but may be required for parsing, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 *
 * \param options A bitset of options composed of the flags in CXReparse_Flags.
 * The function \c clang_defaultReparseOptions() produces a default set of
 * options recommended for most uses, based on the translation unit.
 *
 * \returns 0 if the sources could be reparsed.  A non-zero error code will be
 * returned if reparsing was impossible, such that the translation unit is
 * invalid. In such cases, the only valid call for \c TU is
 * \c clang_disposeTranslationUnit(TU).  The error codes returned by this
 * routine are described by the \c CXErrorCode enum.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_reparseTranslationUnit"; }
};

struct Entity_clang_reparseTranslationUnit
    : public Bind_clang_reparseTranslationUnit<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_reparseTranslationUnit(EntityScope parent_h)
      : Bind_clang_reparseTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_reparseTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_CXTUResourceUsageKind
template <class Pybind11T>
struct Bind_CXTUResourceUsageKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTUResourceUsageKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXTUResourceUsage_AST",
                   CXTUResourceUsageKind::CXTUResourceUsage_AST);
    pb11_obj.value("CXTUResourceUsage_Identifiers",
                   CXTUResourceUsageKind::CXTUResourceUsage_Identifiers);
    pb11_obj.value("CXTUResourceUsage_Selectors",
                   CXTUResourceUsageKind::CXTUResourceUsage_Selectors);
    pb11_obj.value(
        "CXTUResourceUsage_GlobalCompletionResults",
        CXTUResourceUsageKind::CXTUResourceUsage_GlobalCompletionResults);
    pb11_obj.value(
        "CXTUResourceUsage_SourceManagerContentCache",
        CXTUResourceUsageKind::CXTUResourceUsage_SourceManagerContentCache);
    pb11_obj.value("CXTUResourceUsage_AST_SideTables",
                   CXTUResourceUsageKind::CXTUResourceUsage_AST_SideTables);
    pb11_obj.value("CXTUResourceUsage_SourceManager_Membuffer_Malloc",
                   CXTUResourceUsageKind::
                       CXTUResourceUsage_SourceManager_Membuffer_Malloc);
    pb11_obj.value(
        "CXTUResourceUsage_SourceManager_Membuffer_MMap",
        CXTUResourceUsageKind::CXTUResourceUsage_SourceManager_Membuffer_MMap);
    pb11_obj.value("CXTUResourceUsage_ExternalASTSource_Membuffer_Malloc",
                   CXTUResourceUsageKind::
                       CXTUResourceUsage_ExternalASTSource_Membuffer_Malloc);
    pb11_obj.value("CXTUResourceUsage_ExternalASTSource_Membuffer_MMap",
                   CXTUResourceUsageKind::
                       CXTUResourceUsage_ExternalASTSource_Membuffer_MMap);
    pb11_obj.value("CXTUResourceUsage_Preprocessor",
                   CXTUResourceUsageKind::CXTUResourceUsage_Preprocessor);
    pb11_obj.value(
        "CXTUResourceUsage_PreprocessingRecord",
        CXTUResourceUsageKind::CXTUResourceUsage_PreprocessingRecord);
    pb11_obj.value(
        "CXTUResourceUsage_SourceManager_DataStructures",
        CXTUResourceUsageKind::CXTUResourceUsage_SourceManager_DataStructures);
    pb11_obj.value(
        "CXTUResourceUsage_Preprocessor_HeaderSearch",
        CXTUResourceUsageKind::CXTUResourceUsage_Preprocessor_HeaderSearch);
    pb11_obj.value(
        "CXTUResourceUsage_MEMORY_IN_BYTES_BEGIN",
        CXTUResourceUsageKind::CXTUResourceUsage_MEMORY_IN_BYTES_BEGIN);
    pb11_obj.value(
        "CXTUResourceUsage_MEMORY_IN_BYTES_END",
        CXTUResourceUsageKind::CXTUResourceUsage_MEMORY_IN_BYTES_END);
    pb11_obj.value("CXTUResourceUsage_First",
                   CXTUResourceUsageKind::CXTUResourceUsage_First);
    pb11_obj.value("CXTUResourceUsage_Last",
                   CXTUResourceUsageKind::CXTUResourceUsage_Last);
  }

  static const char *Key() { return "CXTUResourceUsageKind"; }
};

struct Entity_CXTUResourceUsageKind
    : public Bind_CXTUResourceUsageKind<
          std::decay_t<pybind11::enum_<CXTUResourceUsageKind>>> {

  explicit Entity_CXTUResourceUsageKind(EntityScope parent_h)
      : Bind_CXTUResourceUsageKind<
            std::decay_t<pybind11::enum_<CXTUResourceUsageKind>>>(parent_h),
        handle{parent_h, "CXTUResourceUsageKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Categorizes how memory is being used by a translation unit.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXTUResourceUsageKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTUResourceUsageKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTUResourceUsageName
template <class Pybind11T>
struct Bind_clang_getTUResourceUsageName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTUResourceUsageName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getTUResourceUsageName",
                 static_cast<const char *(*)(CXTUResourceUsageKind)>(
                     &clang_getTUResourceUsageName),
                 R"_pb11_weaver(/**
 * Returns the human-readable null-terminated C string that represents
 *  the name of the memory category.  This string should never be freed.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTUResourceUsageName"; }
};

struct Entity_clang_getTUResourceUsageName
    : public Bind_clang_getTUResourceUsageName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTUResourceUsageName(EntityScope parent_h)
      : Bind_clang_getTUResourceUsageName<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTUResourceUsageName

#ifndef PB11_WEAVER_DISABLE_Entity_CXTUResourceUsageEntry
template <class Pybind11T>
struct Bind_CXTUResourceUsageEntry : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTUResourceUsageEntry(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/* Amount of resources used.
      The units will depend on the resource kind. */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite(
        "kind", &CXTUResourceUsageEntry::kind,
        R"_pb11_weaver(/* The memory usage category. */)_pb11_weaver");
    pb11_obj.def_readwrite("amount", &CXTUResourceUsageEntry::amount,
                           R"_pb11_weaver(/* Amount of resources used.
      The units will depend on the resource kind. */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXTUResourceUsageEntry"; }
};

struct Entity_CXTUResourceUsageEntry
    : public Bind_CXTUResourceUsageEntry<
          std::decay_t<pybind11::class_<CXTUResourceUsageEntry>>> {

  explicit Entity_CXTUResourceUsageEntry(EntityScope parent_h)
      : Bind_CXTUResourceUsageEntry<
            std::decay_t<pybind11::class_<CXTUResourceUsageEntry>>>(parent_h),
        handle{parent_h, "CXTUResourceUsageEntry", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXTUResourceUsageEntry> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTUResourceUsageEntry

#ifndef PB11_WEAVER_DISABLE_Entity_CXTUResourceUsage
template <class Pybind11T> struct Bind_CXTUResourceUsage : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTUResourceUsage(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        pybind11::init<>(),
        R"_pb11_weaver(/* An array of key-value pairs, representing the breakdown of memory
            usage. */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite(
        "numEntries", &CXTUResourceUsage::numEntries,
        R"_pb11_weaver(/* The number of entries in the 'entries' array. */)_pb11_weaver");
    pb11_obj.def_readwrite(
        "entries", &CXTUResourceUsage::entries,
        R"_pb11_weaver(/* An array of key-value pairs, representing the breakdown of memory
            usage. */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXTUResourceUsage"; }
};

struct Entity_CXTUResourceUsage
    : public Bind_CXTUResourceUsage<
          std::decay_t<pybind11::class_<CXTUResourceUsage>>> {

  explicit Entity_CXTUResourceUsage(EntityScope parent_h)
      : Bind_CXTUResourceUsage<
            std::decay_t<pybind11::class_<CXTUResourceUsage>>>(parent_h),
        handle{parent_h, "CXTUResourceUsage", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * The memory usage of a CXTranslationUnit, broken into categories.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXTUResourceUsage> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTUResourceUsage

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCXTUResourceUsage
template <class Pybind11T>
struct Bind_clang_getCXTUResourceUsage : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCXTUResourceUsage(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCXTUResourceUsage",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU) {
          return clang_getCXTUResourceUsage((TU)->ptr);
        },
        R"_pb11_weaver(/**
 * Return the memory usage of a translation unit.  This object
 *  should be released with clang_disposeCXTUResourceUsage().
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCXTUResourceUsage"; }
};

struct Entity_clang_getCXTUResourceUsage
    : public Bind_clang_getCXTUResourceUsage<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCXTUResourceUsage(EntityScope parent_h)
      : Bind_clang_getCXTUResourceUsage<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCXTUResourceUsage

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeCXTUResourceUsage
template <class Pybind11T>
struct Bind_clang_disposeCXTUResourceUsage : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeCXTUResourceUsage(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_disposeCXTUResourceUsage",
                 static_cast<void (*)(CXTUResourceUsage)>(
                     &clang_disposeCXTUResourceUsage));
  }

  static const char *Key() { return "clang_disposeCXTUResourceUsage"; }
};

struct Entity_clang_disposeCXTUResourceUsage
    : public Bind_clang_disposeCXTUResourceUsage<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeCXTUResourceUsage(EntityScope parent_h)
      : Bind_clang_disposeCXTUResourceUsage<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeCXTUResourceUsage

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTranslationUnitTargetInfo
template <class Pybind11T>
struct Bind_clang_getTranslationUnitTargetInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTranslationUnitTargetInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTranslationUnitTargetInfo",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *CTUnit) {
          return pybind11_weaver::WrapP<CXTargetInfoImpl *>(
                     clang_getTranslationUnitTargetInfo((CTUnit)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * Get target information for this translation unit.
 *
 * The CXTargetInfo object cannot outlive the CXTranslationUnit object.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTranslationUnitTargetInfo"; }
};

struct Entity_clang_getTranslationUnitTargetInfo
    : public Bind_clang_getTranslationUnitTargetInfo<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTranslationUnitTargetInfo(EntityScope parent_h)
      : Bind_clang_getTranslationUnitTargetInfo<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTranslationUnitTargetInfo

#ifndef PB11_WEAVER_DISABLE_Entity_clang_TargetInfo_dispose
template <class Pybind11T>
struct Bind_clang_TargetInfo_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_TargetInfo_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_TargetInfo_dispose",
        [=](pybind11_weaver::PointerWrapper<CXTargetInfoImpl *> *Info) {
          return clang_TargetInfo_dispose((Info)->ptr);
        },
        R"_pb11_weaver(/**
 * Destroy the CXTargetInfo object.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_TargetInfo_dispose"; }
};

struct Entity_clang_TargetInfo_dispose
    : public Bind_clang_TargetInfo_dispose<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_TargetInfo_dispose(EntityScope parent_h)
      : Bind_clang_TargetInfo_dispose<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_TargetInfo_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_clang_TargetInfo_getTriple
template <class Pybind11T>
struct Bind_clang_TargetInfo_getTriple : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_TargetInfo_getTriple(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_TargetInfo_getTriple",
        [=](pybind11_weaver::PointerWrapper<CXTargetInfoImpl *> *Info) {
          return clang_TargetInfo_getTriple((Info)->ptr);
        },
        R"_pb11_weaver(/**
 * Get the normalized target triple as a string.
 *
 * Returns the empty string in case of any error.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_TargetInfo_getTriple"; }
};

struct Entity_clang_TargetInfo_getTriple
    : public Bind_clang_TargetInfo_getTriple<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_TargetInfo_getTriple(EntityScope parent_h)
      : Bind_clang_TargetInfo_getTriple<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_TargetInfo_getTriple

#ifndef PB11_WEAVER_DISABLE_Entity_clang_TargetInfo_getPointerWidth
template <class Pybind11T>
struct Bind_clang_TargetInfo_getPointerWidth : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_TargetInfo_getPointerWidth(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_TargetInfo_getPointerWidth",
        [=](pybind11_weaver::PointerWrapper<CXTargetInfoImpl *> *Info) {
          return clang_TargetInfo_getPointerWidth((Info)->ptr);
        },
        R"_pb11_weaver(/**
 * Get the pointer width of the target in bits.
 *
 * Returns -1 in case of error.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_TargetInfo_getPointerWidth"; }
};

struct Entity_clang_TargetInfo_getPointerWidth
    : public Bind_clang_TargetInfo_getPointerWidth<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_TargetInfo_getPointerWidth(EntityScope parent_h)
      : Bind_clang_TargetInfo_getPointerWidth<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_TargetInfo_getPointerWidth

#ifndef PB11_WEAVER_DISABLE_Entity_CXCursorKind
template <class Pybind11T> struct Bind_CXCursorKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCursorKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCursor_UnexposedDecl",
                   CXCursorKind::CXCursor_UnexposedDecl,
                   R"_pb11_weaver(/* Declarations */
  /**
   * A declaration whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed declarations have the same operations as any other kind
   * of declaration; one can extract their location information,
   * spelling, find their definitions, etc. However, the specific kind
   * of the declaration is not reported.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_StructDecl", CXCursorKind::CXCursor_StructDecl,
                   R"_pb11_weaver(/** A C or C++ struct. */)_pb11_weaver");
    pb11_obj.value("CXCursor_UnionDecl", CXCursorKind::CXCursor_UnionDecl,
                   R"_pb11_weaver(/** A C or C++ union. */)_pb11_weaver");
    pb11_obj.value("CXCursor_ClassDecl", CXCursorKind::CXCursor_ClassDecl,
                   R"_pb11_weaver(/** A C++ class. */)_pb11_weaver");
    pb11_obj.value("CXCursor_EnumDecl", CXCursorKind::CXCursor_EnumDecl,
                   R"_pb11_weaver(/** An enumeration. */)_pb11_weaver");
    pb11_obj.value("CXCursor_FieldDecl", CXCursorKind::CXCursor_FieldDecl,
                   R"_pb11_weaver(/**
   * A field (in C) or non-static data member (in C++) in a
   * struct, union, or C++ class.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_EnumConstantDecl",
                   CXCursorKind::CXCursor_EnumConstantDecl,
                   R"_pb11_weaver(/** An enumerator constant. */)_pb11_weaver");
    pb11_obj.value("CXCursor_FunctionDecl", CXCursorKind::CXCursor_FunctionDecl,
                   R"_pb11_weaver(/** A function. */)_pb11_weaver");
    pb11_obj.value("CXCursor_VarDecl", CXCursorKind::CXCursor_VarDecl,
                   R"_pb11_weaver(/** A variable. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ParmDecl", CXCursorKind::CXCursor_ParmDecl,
        R"_pb11_weaver(/** A function or method parameter. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCInterfaceDecl", CXCursorKind::CXCursor_ObjCInterfaceDecl,
        R"_pb11_weaver(/** An Objective-C \@interface. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCCategoryDecl", CXCursorKind::CXCursor_ObjCCategoryDecl,
        R"_pb11_weaver(/** An Objective-C \@interface for a category. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCProtocolDecl", CXCursorKind::CXCursor_ObjCProtocolDecl,
        R"_pb11_weaver(/** An Objective-C \@protocol declaration. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCPropertyDecl", CXCursorKind::CXCursor_ObjCPropertyDecl,
        R"_pb11_weaver(/** An Objective-C \@property declaration. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCIvarDecl", CXCursorKind::CXCursor_ObjCIvarDecl,
        R"_pb11_weaver(/** An Objective-C instance variable. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCInstanceMethodDecl",
        CXCursorKind::CXCursor_ObjCInstanceMethodDecl,
        R"_pb11_weaver(/** An Objective-C instance method. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCClassMethodDecl",
        CXCursorKind::CXCursor_ObjCClassMethodDecl,
        R"_pb11_weaver(/** An Objective-C class method. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCImplementationDecl",
        CXCursorKind::CXCursor_ObjCImplementationDecl,
        R"_pb11_weaver(/** An Objective-C \@implementation. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCCategoryImplDecl",
        CXCursorKind::CXCursor_ObjCCategoryImplDecl,
        R"_pb11_weaver(/** An Objective-C \@implementation for a category. */)_pb11_weaver");
    pb11_obj.value("CXCursor_TypedefDecl", CXCursorKind::CXCursor_TypedefDecl,
                   R"_pb11_weaver(/** A typedef. */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXMethod", CXCursorKind::CXCursor_CXXMethod,
                   R"_pb11_weaver(/** A C++ class method. */)_pb11_weaver");
    pb11_obj.value("CXCursor_Namespace", CXCursorKind::CXCursor_Namespace,
                   R"_pb11_weaver(/** A C++ namespace. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_LinkageSpec", CXCursorKind::CXCursor_LinkageSpec,
        R"_pb11_weaver(/** A linkage specification, e.g. 'extern "C"'. */)_pb11_weaver");
    pb11_obj.value("CXCursor_Constructor", CXCursorKind::CXCursor_Constructor,
                   R"_pb11_weaver(/** A C++ constructor. */)_pb11_weaver");
    pb11_obj.value("CXCursor_Destructor", CXCursorKind::CXCursor_Destructor,
                   R"_pb11_weaver(/** A C++ destructor. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ConversionFunction",
        CXCursorKind::CXCursor_ConversionFunction,
        R"_pb11_weaver(/** A C++ conversion function. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_TemplateTypeParameter",
        CXCursorKind::CXCursor_TemplateTypeParameter,
        R"_pb11_weaver(/** A C++ template type parameter. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_NonTypeTemplateParameter",
        CXCursorKind::CXCursor_NonTypeTemplateParameter,
        R"_pb11_weaver(/** A C++ non-type template parameter. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_TemplateTemplateParameter",
        CXCursorKind::CXCursor_TemplateTemplateParameter,
        R"_pb11_weaver(/** A C++ template template parameter. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_FunctionTemplate", CXCursorKind::CXCursor_FunctionTemplate,
        R"_pb11_weaver(/** A C++ function template. */)_pb11_weaver");
    pb11_obj.value("CXCursor_ClassTemplate",
                   CXCursorKind::CXCursor_ClassTemplate,
                   R"_pb11_weaver(/** A C++ class template. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ClassTemplatePartialSpecialization",
        CXCursorKind::CXCursor_ClassTemplatePartialSpecialization,
        R"_pb11_weaver(/** A C++ class template partial specialization. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_NamespaceAlias", CXCursorKind::CXCursor_NamespaceAlias,
        R"_pb11_weaver(/** A C++ namespace alias declaration. */)_pb11_weaver");
    pb11_obj.value("CXCursor_UsingDirective",
                   CXCursorKind::CXCursor_UsingDirective,
                   R"_pb11_weaver(/** A C++ using directive. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_UsingDeclaration", CXCursorKind::CXCursor_UsingDeclaration,
        R"_pb11_weaver(/** A C++ using declaration. */)_pb11_weaver");
    pb11_obj.value("CXCursor_TypeAliasDecl",
                   CXCursorKind::CXCursor_TypeAliasDecl,
                   R"_pb11_weaver(/** A C++ alias declaration */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCSynthesizeDecl",
        CXCursorKind::CXCursor_ObjCSynthesizeDecl,
        R"_pb11_weaver(/** An Objective-C \@synthesize definition. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCDynamicDecl", CXCursorKind::CXCursor_ObjCDynamicDecl,
        R"_pb11_weaver(/** An Objective-C \@dynamic definition. */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXAccessSpecifier",
                   CXCursorKind::CXCursor_CXXAccessSpecifier,
                   R"_pb11_weaver(/** An access specifier. */)_pb11_weaver");
    pb11_obj.value("CXCursor_FirstDecl", CXCursorKind::CXCursor_FirstDecl,
                   R"_pb11_weaver(/** An access specifier. */)_pb11_weaver");
    pb11_obj.value("CXCursor_LastDecl", CXCursorKind::CXCursor_LastDecl,
                   R"_pb11_weaver(/** An access specifier. */)_pb11_weaver");
    pb11_obj.value("CXCursor_FirstRef", CXCursorKind::CXCursor_FirstRef,
                   R"_pb11_weaver(/* Decl references */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCSuperClassRef",
                   CXCursorKind::CXCursor_ObjCSuperClassRef);
    pb11_obj.value("CXCursor_ObjCProtocolRef",
                   CXCursorKind::CXCursor_ObjCProtocolRef);
    pb11_obj.value("CXCursor_ObjCClassRef",
                   CXCursorKind::CXCursor_ObjCClassRef);
    pb11_obj.value("CXCursor_TypeRef", CXCursorKind::CXCursor_TypeRef,
                   R"_pb11_weaver(/**
   * A reference to a type declaration.
   *
   * A type reference occurs anywhere where a type is named but not
   * declared. For example, given:
   *
   * \code
   * typedef unsigned size_type;
   * size_type size;
   * \endcode
   *
   * The typedef is a declaration of size_type (CXCursor_TypedefDecl),
   * while the type of the variable "size" is referenced. The cursor
   * referenced by the type of size is the typedef for size_type.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXBaseSpecifier",
                   CXCursorKind::CXCursor_CXXBaseSpecifier, R"_pb11_weaver(/**
   * A reference to a type declaration.
   *
   * A type reference occurs anywhere where a type is named but not
   * declared. For example, given:
   *
   * \code
   * typedef unsigned size_type;
   * size_type size;
   * \endcode
   *
   * The typedef is a declaration of size_type (CXCursor_TypedefDecl),
   * while the type of the variable "size" is referenced. The cursor
   * referenced by the type of size is the typedef for size_type.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_TemplateRef", CXCursorKind::CXCursor_TemplateRef,
                   R"_pb11_weaver(/**
   * A reference to a class template, function template, template
   * template parameter, or class template partial specialization.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_NamespaceRef", CXCursorKind::CXCursor_NamespaceRef,
                   R"_pb11_weaver(/**
   * A reference to a namespace or namespace alias.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_MemberRef", CXCursorKind::CXCursor_MemberRef,
                   R"_pb11_weaver(/**
   * A reference to a member of a struct, union, or class that occurs in
   * some non-expression context, e.g., a designated initializer.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_LabelRef", CXCursorKind::CXCursor_LabelRef,
                   R"_pb11_weaver(/**
   * A reference to a labeled statement.
   *
   * This cursor kind is used to describe the jump to "start_over" in the
   * goto statement in the following example:
   *
   * \code
   *   start_over:
   *     ++counter;
   *
   *     goto start_over;
   * \endcode
   *
   * A label reference cursor refers to a label statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OverloadedDeclRef",
                   CXCursorKind::CXCursor_OverloadedDeclRef, R"_pb11_weaver(/**
   * A reference to a set of overloaded functions or function templates
   * that has not yet been resolved to a specific function or function template.
   *
   * An overloaded declaration reference cursor occurs in C++ templates where
   * a dependent name refers to a function. For example:
   *
   * \code
   * template<typename T> void swap(T&, T&);
   *
   * struct X { ... };
   * void swap(X&, X&);
   *
   * template<typename T>
   * void reverse(T* first, T* last) {
   *   while (first < last - 1) {
   *     swap(*first, *--last);
   *     ++first;
   *   }
   * }
   *
   * struct Y { };
   * void swap(Y&, Y&);
   * \endcode
   *
   * Here, the identifier "swap" is associated with an overloaded declaration
   * reference. In the template definition, "swap" refers to either of the two
   * "swap" functions declared above, so both results will be available. At
   * instantiation time, "swap" may also refer to other functions found via
   * argument-dependent lookup (e.g., the "swap" function at the end of the
   * example).
   *
   * The functions \c clang_getNumOverloadedDecls() and
   * \c clang_getOverloadedDecl() can be used to retrieve the definitions
   * referenced by this cursor.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_VariableRef", CXCursorKind::CXCursor_VariableRef,
                   R"_pb11_weaver(/**
   * A reference to a variable that occurs in some non-expression
   * context, e.g., a C++ lambda capture list.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_LastRef", CXCursorKind::CXCursor_LastRef,
                   R"_pb11_weaver(/**
   * A reference to a variable that occurs in some non-expression
   * context, e.g., a C++ lambda capture list.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_FirstInvalid", CXCursorKind::CXCursor_FirstInvalid,
                   R"_pb11_weaver(/* Error conditions */)_pb11_weaver");
    pb11_obj.value("CXCursor_InvalidFile", CXCursorKind::CXCursor_InvalidFile,
                   R"_pb11_weaver(/* Error conditions */)_pb11_weaver");
    pb11_obj.value("CXCursor_NoDeclFound", CXCursorKind::CXCursor_NoDeclFound,
                   R"_pb11_weaver(/* Error conditions */)_pb11_weaver");
    pb11_obj.value("CXCursor_NotImplemented",
                   CXCursorKind::CXCursor_NotImplemented,
                   R"_pb11_weaver(/* Error conditions */)_pb11_weaver");
    pb11_obj.value("CXCursor_InvalidCode", CXCursorKind::CXCursor_InvalidCode,
                   R"_pb11_weaver(/* Error conditions */)_pb11_weaver");
    pb11_obj.value("CXCursor_LastInvalid", CXCursorKind::CXCursor_LastInvalid,
                   R"_pb11_weaver(/* Error conditions */)_pb11_weaver");
    pb11_obj.value("CXCursor_FirstExpr", CXCursorKind::CXCursor_FirstExpr,
                   R"_pb11_weaver(/* Expressions */)_pb11_weaver");
    pb11_obj.value("CXCursor_UnexposedExpr",
                   CXCursorKind::CXCursor_UnexposedExpr, R"_pb11_weaver(/**
   * An expression whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed expressions have the same operations as any other kind
   * of expression; one can extract their location information,
   * spelling, children, etc. However, the specific kind of the
   * expression is not reported.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_DeclRefExpr", CXCursorKind::CXCursor_DeclRefExpr,
                   R"_pb11_weaver(/**
   * An expression that refers to some value declaration, such
   * as a function, variable, or enumerator.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_MemberRefExpr",
                   CXCursorKind::CXCursor_MemberRefExpr, R"_pb11_weaver(/**
   * An expression that refers to a member of a struct, union,
   * class, Objective-C class, etc.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_CallExpr", CXCursorKind::CXCursor_CallExpr,
        R"_pb11_weaver(/** An expression that calls a function. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCMessageExpr", CXCursorKind::CXCursor_ObjCMessageExpr,
        R"_pb11_weaver(/** An expression that sends a message to an Objective-C
   object or class. */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_BlockExpr", CXCursorKind::CXCursor_BlockExpr,
        R"_pb11_weaver(/** An expression that represents a block literal. */)_pb11_weaver");
    pb11_obj.value("CXCursor_IntegerLiteral",
                   CXCursorKind::CXCursor_IntegerLiteral,
                   R"_pb11_weaver(/** An integer literal.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_FloatingLiteral",
                   CXCursorKind::CXCursor_FloatingLiteral,
                   R"_pb11_weaver(/** A floating point number literal.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ImaginaryLiteral",
                   CXCursorKind::CXCursor_ImaginaryLiteral,
                   R"_pb11_weaver(/** An imaginary number literal.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_StringLiteral",
                   CXCursorKind::CXCursor_StringLiteral,
                   R"_pb11_weaver(/** A string literal.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CharacterLiteral",
                   CXCursorKind::CXCursor_CharacterLiteral,
                   R"_pb11_weaver(/** A character literal.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ParenExpr", CXCursorKind::CXCursor_ParenExpr,
                   R"_pb11_weaver(/** A parenthesized expression, e.g. "(1)".
   *
   * This AST node is only formed if full location information is requested.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_UnaryOperator", CXCursorKind::CXCursor_UnaryOperator,
        R"_pb11_weaver(/** This represents the unary-expression's (except sizeof and
   * alignof).
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ArraySubscriptExpr",
                   CXCursorKind::CXCursor_ArraySubscriptExpr,
                   R"_pb11_weaver(/** [C99 6.5.2.1] Array Subscripting.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_BinaryOperator", CXCursorKind::CXCursor_BinaryOperator,
        R"_pb11_weaver(/** A builtin binary operation expression such as "x + y" or
   * "x <= y".
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CompoundAssignOperator",
                   CXCursorKind::CXCursor_CompoundAssignOperator,
                   R"_pb11_weaver(/** Compound assignment such as "+=".
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ConditionalOperator",
                   CXCursorKind::CXCursor_ConditionalOperator,
                   R"_pb11_weaver(/** The ?: ternary operator.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_CStyleCastExpr", CXCursorKind::CXCursor_CStyleCastExpr,
        R"_pb11_weaver(/** An explicit cast in C (C99 6.5.4) or a C-style cast in C++
   * (C++ [expr.cast]), which uses the syntax (Type)expr.
   *
   * For example: (int)f.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CompoundLiteralExpr",
                   CXCursorKind::CXCursor_CompoundLiteralExpr,
                   R"_pb11_weaver(/** [C99 6.5.2.5]
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_InitListExpr", CXCursorKind::CXCursor_InitListExpr,
                   R"_pb11_weaver(/** Describes an C or C++ initializer list.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_AddrLabelExpr", CXCursorKind::CXCursor_AddrLabelExpr,
        R"_pb11_weaver(/** The GNU address of label extension, representing &&label.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_StmtExpr", CXCursorKind::CXCursor_StmtExpr,
        R"_pb11_weaver(/** This is the GNU Statement Expression extension: ({int X=4; X;})
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_GenericSelectionExpr",
                   CXCursorKind::CXCursor_GenericSelectionExpr,
                   R"_pb11_weaver(/** Represents a C11 generic selection.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_GNUNullExpr", CXCursorKind::CXCursor_GNUNullExpr,
        R"_pb11_weaver(/** Implements the GNU __null extension, which is a name for a null
   * pointer constant that has integral type (e.g., int or long) and is the same
   * size and alignment as a pointer.
   *
   * The __null extension is typically only used by system headers, which define
   * NULL as __null in C++ rather than using 0 (which is an integer that may not
   * match the size of a pointer).
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXStaticCastExpr",
                   CXCursorKind::CXCursor_CXXStaticCastExpr,
                   R"_pb11_weaver(/** C++'s static_cast<> expression.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXDynamicCastExpr",
                   CXCursorKind::CXCursor_CXXDynamicCastExpr,
                   R"_pb11_weaver(/** C++'s dynamic_cast<> expression.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXReinterpretCastExpr",
                   CXCursorKind::CXCursor_CXXReinterpretCastExpr,
                   R"_pb11_weaver(/** C++'s reinterpret_cast<> expression.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXConstCastExpr",
                   CXCursorKind::CXCursor_CXXConstCastExpr,
                   R"_pb11_weaver(/** C++'s const_cast<> expression.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_CXXFunctionalCastExpr",
        CXCursorKind::CXCursor_CXXFunctionalCastExpr,
        R"_pb11_weaver(/** Represents an explicit C++ type conversion that uses "functional"
   * notion (C++ [expr.type.conv]).
   *
   * Example:
   * \code
   *   x = int(0.5);
   * \endcode
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_CXXTypeidExpr", CXCursorKind::CXCursor_CXXTypeidExpr,
        R"_pb11_weaver(/** A C++ typeid expression (C++ [expr.typeid]).
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXBoolLiteralExpr",
                   CXCursorKind::CXCursor_CXXBoolLiteralExpr,
                   R"_pb11_weaver(/** [C++ 2.13.5] C++ Boolean Literal.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXNullPtrLiteralExpr",
                   CXCursorKind::CXCursor_CXXNullPtrLiteralExpr,
                   R"_pb11_weaver(/** [C++0x 2.14.7] C++ Pointer Literal.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXThisExpr", CXCursorKind::CXCursor_CXXThisExpr,
                   R"_pb11_weaver(/** Represents the "this" expression in C++
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXThrowExpr", CXCursorKind::CXCursor_CXXThrowExpr,
                   R"_pb11_weaver(/** [C++ 15] C++ Throw Expression.
   *
   * This handles 'throw' and 'throw' assignment-expression. When
   * assignment-expression isn't present, Op will be null.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_CXXNewExpr", CXCursorKind::CXCursor_CXXNewExpr,
        R"_pb11_weaver(/** A new expression for memory allocation and constructor calls, e.g:
   * "new CXXNewExpr(foo)".
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_CXXDeleteExpr", CXCursorKind::CXCursor_CXXDeleteExpr,
        R"_pb11_weaver(/** A delete expression for memory deallocation and destructor calls,
   * e.g. "delete[] pArray".
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_UnaryExpr", CXCursorKind::CXCursor_UnaryExpr,
        R"_pb11_weaver(/** A unary expression. (noexcept, sizeof, or other traits)
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCStringLiteral",
                   CXCursorKind::CXCursor_ObjCStringLiteral,
                   R"_pb11_weaver(/** An Objective-C string literal i.e. @"foo".
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCEncodeExpr",
                   CXCursorKind::CXCursor_ObjCEncodeExpr,
                   R"_pb11_weaver(/** An Objective-C \@encode expression.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCSelectorExpr",
                   CXCursorKind::CXCursor_ObjCSelectorExpr,
                   R"_pb11_weaver(/** An Objective-C \@selector expression.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCProtocolExpr",
                   CXCursorKind::CXCursor_ObjCProtocolExpr,
                   R"_pb11_weaver(/** An Objective-C \@protocol expression.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCBridgedCastExpr",
        CXCursorKind::CXCursor_ObjCBridgedCastExpr,
        R"_pb11_weaver(/** An Objective-C "bridged" cast expression, which casts between
   * Objective-C pointers and C pointers, transferring ownership in the process.
   *
   * \code
   *   NSString *str = (__bridge_transfer NSString *)CFCreateString();
   * \endcode
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_PackExpansionExpr", CXCursorKind::CXCursor_PackExpansionExpr,
        R"_pb11_weaver(/** Represents a C++0x pack expansion that produces a sequence of
   * expressions.
   *
   * A pack expansion expression contains a pattern (which itself is an
   * expression) followed by an ellipsis. For example:
   *
   * \code
   * template<typename F, typename ...Types>
   * void forward(F f, Types &&...args) {
   *  f(static_cast<Types&&>(args)...);
   * }
   * \endcode
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_SizeOfPackExpr", CXCursorKind::CXCursor_SizeOfPackExpr,
        R"_pb11_weaver(/** Represents an expression that computes the length of a parameter
   * pack.
   *
   * \code
   * template<typename ...Types>
   * struct count {
   *   static const unsigned value = sizeof...(Types);
   * };
   * \endcode
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_LambdaExpr", CXCursorKind::CXCursor_LambdaExpr,
        R"_pb11_weaver(/* Represents a C++ lambda expression that produces a local function
   * object.
   *
   * \code
   * void abssort(float *x, unsigned N) {
   *   std::sort(x, x + N,
   *             [](float a, float b) {
   *               return std::abs(a) < std::abs(b);
   *             });
   * }
   * \endcode
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCBoolLiteralExpr",
                   CXCursorKind::CXCursor_ObjCBoolLiteralExpr,
                   R"_pb11_weaver(/** Objective-c Boolean Literal.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCSelfExpr", CXCursorKind::CXCursor_ObjCSelfExpr,
        R"_pb11_weaver(/** Represents the "self" expression in an Objective-C method.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPArraySectionExpr",
                   CXCursorKind::CXCursor_OMPArraySectionExpr,
                   R"_pb11_weaver(/** OpenMP 5.0 [2.1.5, Array Section].
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCAvailabilityCheckExpr",
                   CXCursorKind::CXCursor_ObjCAvailabilityCheckExpr,
                   R"_pb11_weaver(/** Represents an @available(...) check.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_FixedPointLiteral",
                   CXCursorKind::CXCursor_FixedPointLiteral, R"_pb11_weaver(/**
   * Fixed point literal
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPArrayShapingExpr",
                   CXCursorKind::CXCursor_OMPArrayShapingExpr,
                   R"_pb11_weaver(/** OpenMP 5.0 [2.1.4, Array Shaping].
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPIteratorExpr",
                   CXCursorKind::CXCursor_OMPIteratorExpr, R"_pb11_weaver(/**
   * OpenMP 5.0 [2.1.6 Iterators]
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXAddrspaceCastExpr",
                   CXCursorKind::CXCursor_CXXAddrspaceCastExpr,
                   R"_pb11_weaver(/** OpenCL's addrspace_cast<> expression.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ConceptSpecializationExpr",
                   CXCursorKind::CXCursor_ConceptSpecializationExpr,
                   R"_pb11_weaver(/**
   * Expression that references a C++20 concept.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_RequiresExpr", CXCursorKind::CXCursor_RequiresExpr,
                   R"_pb11_weaver(/**
   * Expression that references a C++20 concept.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXParenListInitExpr",
                   CXCursorKind::CXCursor_CXXParenListInitExpr,
                   R"_pb11_weaver(/**
   * Expression that references a C++20 parenthesized list aggregate
   * initializer.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_LastExpr", CXCursorKind::CXCursor_LastExpr,
                   R"_pb11_weaver(/**
   * Expression that references a C++20 parenthesized list aggregate
   * initializer.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_FirstStmt", CXCursorKind::CXCursor_FirstStmt,
                   R"_pb11_weaver(/* Statements */)_pb11_weaver");
    pb11_obj.value("CXCursor_UnexposedStmt",
                   CXCursorKind::CXCursor_UnexposedStmt, R"_pb11_weaver(/**
   * A statement whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed statements have the same operations as any other kind of
   * statement; one can extract their location information, spelling,
   * children, etc. However, the specific kind of the statement is not
   * reported.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_LabelStmt", CXCursorKind::CXCursor_LabelStmt,
                   R"_pb11_weaver(/** A labelled statement in a function.
   *
   * This cursor kind is used to describe the "start_over:" label statement in
   * the following example:
   *
   * \code
   *   start_over:
   *     ++counter;
   * \endcode
   *
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CompoundStmt", CXCursorKind::CXCursor_CompoundStmt,
                   R"_pb11_weaver(/** A group of statements like { stmt stmt }.
   *
   * This cursor kind is used to describe compound statements, e.g. function
   * bodies.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CaseStmt", CXCursorKind::CXCursor_CaseStmt,
                   R"_pb11_weaver(/** A case statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_DefaultStmt", CXCursorKind::CXCursor_DefaultStmt,
                   R"_pb11_weaver(/** A default statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_IfStmt", CXCursorKind::CXCursor_IfStmt,
                   R"_pb11_weaver(/** An if statement
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_SwitchStmt", CXCursorKind::CXCursor_SwitchStmt,
                   R"_pb11_weaver(/** A switch statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_WhileStmt", CXCursorKind::CXCursor_WhileStmt,
                   R"_pb11_weaver(/** A while statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_DoStmt", CXCursorKind::CXCursor_DoStmt,
                   R"_pb11_weaver(/** A do statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ForStmt", CXCursorKind::CXCursor_ForStmt,
                   R"_pb11_weaver(/** A for statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_GotoStmt", CXCursorKind::CXCursor_GotoStmt,
                   R"_pb11_weaver(/** A goto statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_IndirectGotoStmt",
                   CXCursorKind::CXCursor_IndirectGotoStmt,
                   R"_pb11_weaver(/** An indirect goto statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ContinueStmt", CXCursorKind::CXCursor_ContinueStmt,
                   R"_pb11_weaver(/** A continue statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_BreakStmt", CXCursorKind::CXCursor_BreakStmt,
                   R"_pb11_weaver(/** A break statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ReturnStmt", CXCursorKind::CXCursor_ReturnStmt,
                   R"_pb11_weaver(/** A return statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_GCCAsmStmt", CXCursorKind::CXCursor_GCCAsmStmt,
                   R"_pb11_weaver(/** A GCC inline assembly statement extension.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_AsmStmt", CXCursorKind::CXCursor_AsmStmt,
                   R"_pb11_weaver(/** A GCC inline assembly statement extension.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_ObjCAtTryStmt", CXCursorKind::CXCursor_ObjCAtTryStmt,
        R"_pb11_weaver(/** Objective-C's overall \@try-\@catch-\@finally statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCAtCatchStmt",
                   CXCursorKind::CXCursor_ObjCAtCatchStmt,
                   R"_pb11_weaver(/** Objective-C's \@catch statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCAtFinallyStmt",
                   CXCursorKind::CXCursor_ObjCAtFinallyStmt,
                   R"_pb11_weaver(/** Objective-C's \@finally statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCAtThrowStmt",
                   CXCursorKind::CXCursor_ObjCAtThrowStmt,
                   R"_pb11_weaver(/** Objective-C's \@throw statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCAtSynchronizedStmt",
                   CXCursorKind::CXCursor_ObjCAtSynchronizedStmt,
                   R"_pb11_weaver(/** Objective-C's \@synchronized statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCAutoreleasePoolStmt",
                   CXCursorKind::CXCursor_ObjCAutoreleasePoolStmt,
                   R"_pb11_weaver(/** Objective-C's autorelease pool statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCForCollectionStmt",
                   CXCursorKind::CXCursor_ObjCForCollectionStmt,
                   R"_pb11_weaver(/** Objective-C's collection statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXCatchStmt", CXCursorKind::CXCursor_CXXCatchStmt,
                   R"_pb11_weaver(/** C++'s catch statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXTryStmt", CXCursorKind::CXCursor_CXXTryStmt,
                   R"_pb11_weaver(/** C++'s try statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXForRangeStmt",
                   CXCursorKind::CXCursor_CXXForRangeStmt,
                   R"_pb11_weaver(/** C++'s for (* : *) statement.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_SEHTryStmt", CXCursorKind::CXCursor_SEHTryStmt,
        R"_pb11_weaver(/** Windows Structured Exception Handling's try statement.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_SEHExceptStmt", CXCursorKind::CXCursor_SEHExceptStmt,
        R"_pb11_weaver(/** Windows Structured Exception Handling's except statement.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_SEHFinallyStmt", CXCursorKind::CXCursor_SEHFinallyStmt,
        R"_pb11_weaver(/** Windows Structured Exception Handling's finally statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_MSAsmStmt", CXCursorKind::CXCursor_MSAsmStmt,
                   R"_pb11_weaver(/** A MS inline assembly statement extension.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_NullStmt", CXCursorKind::CXCursor_NullStmt,
                   R"_pb11_weaver(/** The null statement ";": C99 6.8.3p3.
   *
   * This cursor kind is used to describe the null statement.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_DeclStmt", CXCursorKind::CXCursor_DeclStmt,
        R"_pb11_weaver(/** Adaptor class for mixing declarations with statements and
   * expressions.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelDirective",
                   CXCursorKind::CXCursor_OMPParallelDirective,
                   R"_pb11_weaver(/** OpenMP parallel directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPSimdDirective",
                   CXCursorKind::CXCursor_OMPSimdDirective,
                   R"_pb11_weaver(/** OpenMP SIMD directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPForDirective",
                   CXCursorKind::CXCursor_OMPForDirective,
                   R"_pb11_weaver(/** OpenMP for directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPSectionsDirective",
                   CXCursorKind::CXCursor_OMPSectionsDirective,
                   R"_pb11_weaver(/** OpenMP sections directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPSectionDirective",
                   CXCursorKind::CXCursor_OMPSectionDirective,
                   R"_pb11_weaver(/** OpenMP section directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPSingleDirective",
                   CXCursorKind::CXCursor_OMPSingleDirective,
                   R"_pb11_weaver(/** OpenMP single directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelForDirective",
                   CXCursorKind::CXCursor_OMPParallelForDirective,
                   R"_pb11_weaver(/** OpenMP parallel for directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelSectionsDirective",
                   CXCursorKind::CXCursor_OMPParallelSectionsDirective,
                   R"_pb11_weaver(/** OpenMP parallel sections directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTaskDirective",
                   CXCursorKind::CXCursor_OMPTaskDirective,
                   R"_pb11_weaver(/** OpenMP task directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPMasterDirective",
                   CXCursorKind::CXCursor_OMPMasterDirective,
                   R"_pb11_weaver(/** OpenMP master directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPCriticalDirective",
                   CXCursorKind::CXCursor_OMPCriticalDirective,
                   R"_pb11_weaver(/** OpenMP critical directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTaskyieldDirective",
                   CXCursorKind::CXCursor_OMPTaskyieldDirective,
                   R"_pb11_weaver(/** OpenMP taskyield directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPBarrierDirective",
                   CXCursorKind::CXCursor_OMPBarrierDirective,
                   R"_pb11_weaver(/** OpenMP barrier directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTaskwaitDirective",
                   CXCursorKind::CXCursor_OMPTaskwaitDirective,
                   R"_pb11_weaver(/** OpenMP taskwait directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPFlushDirective",
                   CXCursorKind::CXCursor_OMPFlushDirective,
                   R"_pb11_weaver(/** OpenMP flush directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_SEHLeaveStmt", CXCursorKind::CXCursor_SEHLeaveStmt,
        R"_pb11_weaver(/** Windows Structured Exception Handling's leave statement.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPOrderedDirective",
                   CXCursorKind::CXCursor_OMPOrderedDirective,
                   R"_pb11_weaver(/** OpenMP ordered directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPAtomicDirective",
                   CXCursorKind::CXCursor_OMPAtomicDirective,
                   R"_pb11_weaver(/** OpenMP atomic directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPForSimdDirective",
                   CXCursorKind::CXCursor_OMPForSimdDirective,
                   R"_pb11_weaver(/** OpenMP for SIMD directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelForSimdDirective",
                   CXCursorKind::CXCursor_OMPParallelForSimdDirective,
                   R"_pb11_weaver(/** OpenMP parallel for SIMD directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetDirective",
                   CXCursorKind::CXCursor_OMPTargetDirective,
                   R"_pb11_weaver(/** OpenMP target directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTeamsDirective",
                   CXCursorKind::CXCursor_OMPTeamsDirective,
                   R"_pb11_weaver(/** OpenMP teams directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTaskgroupDirective",
                   CXCursorKind::CXCursor_OMPTaskgroupDirective,
                   R"_pb11_weaver(/** OpenMP taskgroup directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPCancellationPointDirective",
                   CXCursorKind::CXCursor_OMPCancellationPointDirective,
                   R"_pb11_weaver(/** OpenMP cancellation point directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPCancelDirective",
                   CXCursorKind::CXCursor_OMPCancelDirective,
                   R"_pb11_weaver(/** OpenMP cancel directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetDataDirective",
                   CXCursorKind::CXCursor_OMPTargetDataDirective,
                   R"_pb11_weaver(/** OpenMP target data directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTaskLoopDirective",
                   CXCursorKind::CXCursor_OMPTaskLoopDirective,
                   R"_pb11_weaver(/** OpenMP taskloop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTaskLoopSimdDirective",
                   CXCursorKind::CXCursor_OMPTaskLoopSimdDirective,
                   R"_pb11_weaver(/** OpenMP taskloop simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPDistributeDirective",
                   CXCursorKind::CXCursor_OMPDistributeDirective,
                   R"_pb11_weaver(/** OpenMP distribute directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetEnterDataDirective",
                   CXCursorKind::CXCursor_OMPTargetEnterDataDirective,
                   R"_pb11_weaver(/** OpenMP target enter data directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetExitDataDirective",
                   CXCursorKind::CXCursor_OMPTargetExitDataDirective,
                   R"_pb11_weaver(/** OpenMP target exit data directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetParallelDirective",
                   CXCursorKind::CXCursor_OMPTargetParallelDirective,
                   R"_pb11_weaver(/** OpenMP target parallel directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetParallelForDirective",
                   CXCursorKind::CXCursor_OMPTargetParallelForDirective,
                   R"_pb11_weaver(/** OpenMP target parallel for directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetUpdateDirective",
                   CXCursorKind::CXCursor_OMPTargetUpdateDirective,
                   R"_pb11_weaver(/** OpenMP target update directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPDistributeParallelForDirective",
                   CXCursorKind::CXCursor_OMPDistributeParallelForDirective,
                   R"_pb11_weaver(/** OpenMP distribute parallel for directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_OMPDistributeParallelForSimdDirective",
        CXCursorKind::CXCursor_OMPDistributeParallelForSimdDirective,
        R"_pb11_weaver(/** OpenMP distribute parallel for simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPDistributeSimdDirective",
                   CXCursorKind::CXCursor_OMPDistributeSimdDirective,
                   R"_pb11_weaver(/** OpenMP distribute simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetParallelForSimdDirective",
                   CXCursorKind::CXCursor_OMPTargetParallelForSimdDirective,
                   R"_pb11_weaver(/** OpenMP target parallel for simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetSimdDirective",
                   CXCursorKind::CXCursor_OMPTargetSimdDirective,
                   R"_pb11_weaver(/** OpenMP target simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTeamsDistributeDirective",
                   CXCursorKind::CXCursor_OMPTeamsDistributeDirective,
                   R"_pb11_weaver(/** OpenMP teams distribute directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTeamsDistributeSimdDirective",
                   CXCursorKind::CXCursor_OMPTeamsDistributeSimdDirective,
                   R"_pb11_weaver(/** OpenMP teams distribute simd directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_OMPTeamsDistributeParallelForSimdDirective",
        CXCursorKind::CXCursor_OMPTeamsDistributeParallelForSimdDirective,
        R"_pb11_weaver(/** OpenMP teams distribute parallel for simd directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_OMPTeamsDistributeParallelForDirective",
        CXCursorKind::CXCursor_OMPTeamsDistributeParallelForDirective,
        R"_pb11_weaver(/** OpenMP teams distribute parallel for directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetTeamsDirective",
                   CXCursorKind::CXCursor_OMPTargetTeamsDirective,
                   R"_pb11_weaver(/** OpenMP target teams directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetTeamsDistributeDirective",
                   CXCursorKind::CXCursor_OMPTargetTeamsDistributeDirective,
                   R"_pb11_weaver(/** OpenMP target teams distribute directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_OMPTargetTeamsDistributeParallelForDirective",
        CXCursorKind::CXCursor_OMPTargetTeamsDistributeParallelForDirective,
        R"_pb11_weaver(/** OpenMP target teams distribute parallel for directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_OMPTargetTeamsDistributeParallelForSimdDirective",
        CXCursorKind::CXCursor_OMPTargetTeamsDistributeParallelForSimdDirective,
        R"_pb11_weaver(/** OpenMP target teams distribute parallel for simd directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_OMPTargetTeamsDistributeSimdDirective",
        CXCursorKind::CXCursor_OMPTargetTeamsDistributeSimdDirective,
        R"_pb11_weaver(/** OpenMP target teams distribute simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_BuiltinBitCastExpr",
                   CXCursorKind::CXCursor_BuiltinBitCastExpr,
                   R"_pb11_weaver(/** C++2a std::bit_cast expression.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPMasterTaskLoopDirective",
                   CXCursorKind::CXCursor_OMPMasterTaskLoopDirective,
                   R"_pb11_weaver(/** OpenMP master taskloop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelMasterTaskLoopDirective",
                   CXCursorKind::CXCursor_OMPParallelMasterTaskLoopDirective,
                   R"_pb11_weaver(/** OpenMP parallel master taskloop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPMasterTaskLoopSimdDirective",
                   CXCursorKind::CXCursor_OMPMasterTaskLoopSimdDirective,
                   R"_pb11_weaver(/** OpenMP master taskloop simd directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_OMPParallelMasterTaskLoopSimdDirective",
        CXCursorKind::CXCursor_OMPParallelMasterTaskLoopSimdDirective,
        R"_pb11_weaver(/** OpenMP parallel master taskloop simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelMasterDirective",
                   CXCursorKind::CXCursor_OMPParallelMasterDirective,
                   R"_pb11_weaver(/** OpenMP parallel master directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPDepobjDirective",
                   CXCursorKind::CXCursor_OMPDepobjDirective,
                   R"_pb11_weaver(/** OpenMP depobj directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPScanDirective",
                   CXCursorKind::CXCursor_OMPScanDirective,
                   R"_pb11_weaver(/** OpenMP scan directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTileDirective",
                   CXCursorKind::CXCursor_OMPTileDirective,
                   R"_pb11_weaver(/** OpenMP tile directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPCanonicalLoop",
                   CXCursorKind::CXCursor_OMPCanonicalLoop,
                   R"_pb11_weaver(/** OpenMP canonical loop.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPInteropDirective",
                   CXCursorKind::CXCursor_OMPInteropDirective,
                   R"_pb11_weaver(/** OpenMP interop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPDispatchDirective",
                   CXCursorKind::CXCursor_OMPDispatchDirective,
                   R"_pb11_weaver(/** OpenMP dispatch directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPMaskedDirective",
                   CXCursorKind::CXCursor_OMPMaskedDirective,
                   R"_pb11_weaver(/** OpenMP masked directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPUnrollDirective",
                   CXCursorKind::CXCursor_OMPUnrollDirective,
                   R"_pb11_weaver(/** OpenMP unroll directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPMetaDirective",
                   CXCursorKind::CXCursor_OMPMetaDirective,
                   R"_pb11_weaver(/** OpenMP metadirective directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPGenericLoopDirective",
                   CXCursorKind::CXCursor_OMPGenericLoopDirective,
                   R"_pb11_weaver(/** OpenMP loop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTeamsGenericLoopDirective",
                   CXCursorKind::CXCursor_OMPTeamsGenericLoopDirective,
                   R"_pb11_weaver(/** OpenMP teams loop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetTeamsGenericLoopDirective",
                   CXCursorKind::CXCursor_OMPTargetTeamsGenericLoopDirective,
                   R"_pb11_weaver(/** OpenMP target teams loop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelGenericLoopDirective",
                   CXCursorKind::CXCursor_OMPParallelGenericLoopDirective,
                   R"_pb11_weaver(/** OpenMP parallel loop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPTargetParallelGenericLoopDirective",
                   CXCursorKind::CXCursor_OMPTargetParallelGenericLoopDirective,
                   R"_pb11_weaver(/** OpenMP target parallel loop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelMaskedDirective",
                   CXCursorKind::CXCursor_OMPParallelMaskedDirective,
                   R"_pb11_weaver(/** OpenMP parallel masked directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPMaskedTaskLoopDirective",
                   CXCursorKind::CXCursor_OMPMaskedTaskLoopDirective,
                   R"_pb11_weaver(/** OpenMP masked taskloop directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPMaskedTaskLoopSimdDirective",
                   CXCursorKind::CXCursor_OMPMaskedTaskLoopSimdDirective,
                   R"_pb11_weaver(/** OpenMP masked taskloop simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPParallelMaskedTaskLoopDirective",
                   CXCursorKind::CXCursor_OMPParallelMaskedTaskLoopDirective,
                   R"_pb11_weaver(/** OpenMP parallel masked taskloop directive.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCursor_OMPParallelMaskedTaskLoopSimdDirective",
        CXCursorKind::CXCursor_OMPParallelMaskedTaskLoopSimdDirective,
        R"_pb11_weaver(/** OpenMP parallel masked taskloop simd directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OMPErrorDirective",
                   CXCursorKind::CXCursor_OMPErrorDirective,
                   R"_pb11_weaver(/** OpenMP error directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_LastStmt", CXCursorKind::CXCursor_LastStmt,
                   R"_pb11_weaver(/** OpenMP error directive.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_TranslationUnit",
                   CXCursorKind::CXCursor_TranslationUnit, R"_pb11_weaver(/**
   * Cursor that represents the translation unit itself.
   *
   * The translation unit cursor exists primarily to act as the root
   * cursor for traversing the contents of a translation unit.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_FirstAttr", CXCursorKind::CXCursor_FirstAttr,
                   R"_pb11_weaver(/* Attributes */)_pb11_weaver");
    pb11_obj.value("CXCursor_UnexposedAttr",
                   CXCursorKind::CXCursor_UnexposedAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_IBActionAttr", CXCursorKind::CXCursor_IBActionAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_IBOutletAttr", CXCursorKind::CXCursor_IBOutletAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_IBOutletCollectionAttr",
                   CXCursorKind::CXCursor_IBOutletCollectionAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXFinalAttr", CXCursorKind::CXCursor_CXXFinalAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CXXOverrideAttr",
                   CXCursorKind::CXCursor_CXXOverrideAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_AnnotateAttr", CXCursorKind::CXCursor_AnnotateAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_AsmLabelAttr", CXCursorKind::CXCursor_AsmLabelAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_PackedAttr", CXCursorKind::CXCursor_PackedAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_PureAttr", CXCursorKind::CXCursor_PureAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ConstAttr", CXCursorKind::CXCursor_ConstAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_NoDuplicateAttr",
                   CXCursorKind::CXCursor_NoDuplicateAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CUDAConstantAttr",
                   CXCursorKind::CXCursor_CUDAConstantAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CUDADeviceAttr",
                   CXCursorKind::CXCursor_CUDADeviceAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CUDAGlobalAttr",
                   CXCursorKind::CXCursor_CUDAGlobalAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CUDAHostAttr", CXCursorKind::CXCursor_CUDAHostAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_CUDASharedAttr",
                   CXCursorKind::CXCursor_CUDASharedAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_VisibilityAttr",
                   CXCursorKind::CXCursor_VisibilityAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_DLLExport", CXCursorKind::CXCursor_DLLExport,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_DLLImport", CXCursorKind::CXCursor_DLLImport,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_NSReturnsRetained",
                   CXCursorKind::CXCursor_NSReturnsRetained, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_NSReturnsNotRetained",
                   CXCursorKind::CXCursor_NSReturnsNotRetained,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_NSReturnsAutoreleased",
                   CXCursorKind::CXCursor_NSReturnsAutoreleased,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_NSConsumesSelf",
                   CXCursorKind::CXCursor_NSConsumesSelf, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_NSConsumed", CXCursorKind::CXCursor_NSConsumed,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCException",
                   CXCursorKind::CXCursor_ObjCException, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCNSObject", CXCursorKind::CXCursor_ObjCNSObject,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCIndependentClass",
                   CXCursorKind::CXCursor_ObjCIndependentClass,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCPreciseLifetime",
                   CXCursorKind::CXCursor_ObjCPreciseLifetime,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCReturnsInnerPointer",
                   CXCursorKind::CXCursor_ObjCReturnsInnerPointer,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCRequiresSuper",
                   CXCursorKind::CXCursor_ObjCRequiresSuper, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCRootClass",
                   CXCursorKind::CXCursor_ObjCRootClass, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCSubclassingRestricted",
                   CXCursorKind::CXCursor_ObjCSubclassingRestricted,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCExplicitProtocolImpl",
                   CXCursorKind::CXCursor_ObjCExplicitProtocolImpl,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCDesignatedInitializer",
                   CXCursorKind::CXCursor_ObjCDesignatedInitializer,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCRuntimeVisible",
                   CXCursorKind::CXCursor_ObjCRuntimeVisible, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ObjCBoxable", CXCursorKind::CXCursor_ObjCBoxable,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_FlagEnum", CXCursorKind::CXCursor_FlagEnum,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ConvergentAttr",
                   CXCursorKind::CXCursor_ConvergentAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_WarnUnusedAttr",
                   CXCursorKind::CXCursor_WarnUnusedAttr, R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_WarnUnusedResultAttr",
                   CXCursorKind::CXCursor_WarnUnusedResultAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_AlignedAttr", CXCursorKind::CXCursor_AlignedAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_LastAttr", CXCursorKind::CXCursor_LastAttr,
                   R"_pb11_weaver(/**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_PreprocessingDirective",
                   CXCursorKind::CXCursor_PreprocessingDirective,
                   R"_pb11_weaver(/* Preprocessing */)_pb11_weaver");
    pb11_obj.value("CXCursor_MacroDefinition",
                   CXCursorKind::CXCursor_MacroDefinition,
                   R"_pb11_weaver(/* Preprocessing */)_pb11_weaver");
    pb11_obj.value("CXCursor_MacroExpansion",
                   CXCursorKind::CXCursor_MacroExpansion,
                   R"_pb11_weaver(/* Preprocessing */)_pb11_weaver");
    pb11_obj.value("CXCursor_MacroInstantiation",
                   CXCursorKind::CXCursor_MacroInstantiation,
                   R"_pb11_weaver(/* Preprocessing */)_pb11_weaver");
    pb11_obj.value("CXCursor_InclusionDirective",
                   CXCursorKind::CXCursor_InclusionDirective,
                   R"_pb11_weaver(/* Preprocessing */)_pb11_weaver");
    pb11_obj.value("CXCursor_FirstPreprocessing",
                   CXCursorKind::CXCursor_FirstPreprocessing,
                   R"_pb11_weaver(/* Preprocessing */)_pb11_weaver");
    pb11_obj.value("CXCursor_LastPreprocessing",
                   CXCursorKind::CXCursor_LastPreprocessing,
                   R"_pb11_weaver(/* Preprocessing */)_pb11_weaver");
    pb11_obj.value("CXCursor_ModuleImportDecl",
                   CXCursorKind::CXCursor_ModuleImportDecl,
                   R"_pb11_weaver(/* Extra Declarations */
  /**
   * A module import declaration.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_TypeAliasTemplateDecl",
                   CXCursorKind::CXCursor_TypeAliasTemplateDecl,
                   R"_pb11_weaver(/* Extra Declarations */
  /**
   * A module import declaration.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_StaticAssert", CXCursorKind::CXCursor_StaticAssert,
                   R"_pb11_weaver(/**
   * A static_assert or _Static_assert node
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_FriendDecl", CXCursorKind::CXCursor_FriendDecl,
                   R"_pb11_weaver(/**
   * a friend declaration.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_ConceptDecl", CXCursorKind::CXCursor_ConceptDecl,
                   R"_pb11_weaver(/**
   * a concept declaration.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_FirstExtraDecl",
                   CXCursorKind::CXCursor_FirstExtraDecl, R"_pb11_weaver(/**
   * a concept declaration.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_LastExtraDecl",
                   CXCursorKind::CXCursor_LastExtraDecl, R"_pb11_weaver(/**
   * a concept declaration.
   */)_pb11_weaver");
    pb11_obj.value("CXCursor_OverloadCandidate",
                   CXCursorKind::CXCursor_OverloadCandidate, R"_pb11_weaver(/**
   * A code completion overload candidate.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCursorKind"; }
};

struct Entity_CXCursorKind
    : public Bind_CXCursorKind<std::decay_t<pybind11::enum_<CXCursorKind>>> {

  explicit Entity_CXCursorKind(EntityScope parent_h)
      : Bind_CXCursorKind<std::decay_t<pybind11::enum_<CXCursorKind>>>(
            parent_h),
        handle{parent_h, "CXCursorKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the kind of entity that a cursor refers to.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCursorKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCursorKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXCursor
template <class Pybind11T> struct Bind_CXCursor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCursor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("kind", &CXCursor::kind);
    pb11_obj.def_readwrite("xdata", &CXCursor::xdata);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXCursor"; }
};

struct Entity_CXCursor
    : public Bind_CXCursor<std::decay_t<pybind11::class_<CXCursor>>> {

  explicit Entity_CXCursor(EntityScope parent_h)
      : Bind_CXCursor<std::decay_t<pybind11::class_<CXCursor>>>(parent_h),
        handle{parent_h, "CXCursor", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * A cursor representing some element in the abstract syntax tree for
 * a translation unit.
 *
 * The cursor abstraction unifies the different kinds of entities in a
 * program--declaration, statements, expressions, references to declarations,
 * etc.--under a single "cursor" abstraction with a common set of operations.
 * Common operation for a cursor include: getting the physical location in
 * a source file where the cursor points, getting the name associated with a
 * cursor, and retrieving cursors for any child nodes of a particular cursor.
 *
 * Cursors can be produced in two specific ways.
 * clang_getTranslationUnitCursor() produces a cursor for a translation unit,
 * from which one can use clang_visitChildren() to explore the rest of the
 * translation unit. clang_getCursor() maps from a physical source location
 * to the entity that resides at that location, allowing one to map from the
 * source code into the AST.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXCursor> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCursor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNullCursor
template <class Pybind11T> struct Bind_clang_getNullCursor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNullCursor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getNullCursor",
                 static_cast<CXCursor (*)()>(&clang_getNullCursor),
                 R"_pb11_weaver(/**
 * Retrieve the NULL cursor, which represents no entity.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNullCursor"; }
};

struct Entity_clang_getNullCursor
    : public Bind_clang_getNullCursor<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNullCursor(EntityScope parent_h)
      : Bind_clang_getNullCursor<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNullCursor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTranslationUnitCursor
template <class Pybind11T>
struct Bind_clang_getTranslationUnitCursor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTranslationUnitCursor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTranslationUnitCursor",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0) {
          return clang_getTranslationUnitCursor((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the cursor that represents the given translation unit.
 *
 * The translation unit cursor can be used to start traversing the
 * various declarations within the given translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTranslationUnitCursor"; }
};

struct Entity_clang_getTranslationUnitCursor
    : public Bind_clang_getTranslationUnitCursor<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTranslationUnitCursor(EntityScope parent_h)
      : Bind_clang_getTranslationUnitCursor<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTranslationUnitCursor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_equalCursors
template <class Pybind11T> struct Bind_clang_equalCursors : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_equalCursors(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_equalCursors",
        static_cast<unsigned int (*)(CXCursor, CXCursor)>(&clang_equalCursors),
        R"_pb11_weaver(/**
 * Determine whether two cursors are equivalent.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_equalCursors"; }
};

struct Entity_clang_equalCursors
    : public Bind_clang_equalCursors<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_equalCursors(EntityScope parent_h)
      : Bind_clang_equalCursors<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_equalCursors

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isNull
template <class Pybind11T> struct Bind_clang_Cursor_isNull : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isNull(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_isNull",
                 static_cast<int (*)(CXCursor)>(&clang_Cursor_isNull),
                 R"_pb11_weaver(/**
 * Returns non-zero if \p cursor is null.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isNull"; }
};

struct Entity_clang_Cursor_isNull
    : public Bind_clang_Cursor_isNull<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isNull(EntityScope parent_h)
      : Bind_clang_Cursor_isNull<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isNull

#ifndef PB11_WEAVER_DISABLE_Entity_clang_hashCursor
template <class Pybind11T> struct Bind_clang_hashCursor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_hashCursor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_hashCursor",
                 static_cast<unsigned int (*)(CXCursor)>(&clang_hashCursor),
                 R"_pb11_weaver(/**
 * Compute a hash value for the given cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_hashCursor"; }
};

struct Entity_clang_hashCursor
    : public Bind_clang_hashCursor<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_hashCursor(EntityScope parent_h)
      : Bind_clang_hashCursor<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_hashCursor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorKind
template <class Pybind11T> struct Bind_clang_getCursorKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCursorKind",
                 static_cast<CXCursorKind (*)(CXCursor)>(&clang_getCursorKind),
                 R"_pb11_weaver(/**
 * Retrieve the kind of the given cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorKind"; }
};

struct Entity_clang_getCursorKind
    : public Bind_clang_getCursorKind<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorKind(EntityScope parent_h)
      : Bind_clang_getCursorKind<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isDeclaration
template <class Pybind11T> struct Bind_clang_isDeclaration : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isDeclaration(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isDeclaration",
        static_cast<unsigned int (*)(CXCursorKind)>(&clang_isDeclaration),
        R"_pb11_weaver(/**
 * Determine whether the given cursor kind represents a declaration.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isDeclaration"; }
};

struct Entity_clang_isDeclaration
    : public Bind_clang_isDeclaration<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isDeclaration(EntityScope parent_h)
      : Bind_clang_isDeclaration<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isDeclaration

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isInvalidDeclaration
template <class Pybind11T>
struct Bind_clang_isInvalidDeclaration : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isInvalidDeclaration(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isInvalidDeclaration",
        static_cast<unsigned int (*)(CXCursor)>(&clang_isInvalidDeclaration),
        R"_pb11_weaver(/**
 * Determine whether the given declaration is invalid.
 *
 * A declaration is invalid if it could not be parsed successfully.
 *
 * \returns non-zero if the cursor represents a declaration and it is
 * invalid, otherwise NULL.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isInvalidDeclaration"; }
};

struct Entity_clang_isInvalidDeclaration
    : public Bind_clang_isInvalidDeclaration<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isInvalidDeclaration(EntityScope parent_h)
      : Bind_clang_isInvalidDeclaration<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isInvalidDeclaration

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isReference
template <class Pybind11T> struct Bind_clang_isReference : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isReference(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isReference",
        static_cast<unsigned int (*)(CXCursorKind)>(&clang_isReference),
        R"_pb11_weaver(/**
 * Determine whether the given cursor kind represents a simple
 * reference.
 *
 * Note that other kinds of cursors (such as expressions) can also refer to
 * other cursors. Use clang_getCursorReferenced() to determine whether a
 * particular cursor refers to another entity.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isReference"; }
};

struct Entity_clang_isReference
    : public Bind_clang_isReference<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isReference(EntityScope parent_h)
      : Bind_clang_isReference<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isReference

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isExpression
template <class Pybind11T> struct Bind_clang_isExpression : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isExpression(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isExpression",
        static_cast<unsigned int (*)(CXCursorKind)>(&clang_isExpression),
        R"_pb11_weaver(/**
 * Determine whether the given cursor kind represents an expression.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isExpression"; }
};

struct Entity_clang_isExpression
    : public Bind_clang_isExpression<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isExpression(EntityScope parent_h)
      : Bind_clang_isExpression<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isExpression

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isStatement
template <class Pybind11T> struct Bind_clang_isStatement : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isStatement(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isStatement",
        static_cast<unsigned int (*)(CXCursorKind)>(&clang_isStatement),
        R"_pb11_weaver(/**
 * Determine whether the given cursor kind represents a statement.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isStatement"; }
};

struct Entity_clang_isStatement
    : public Bind_clang_isStatement<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isStatement(EntityScope parent_h)
      : Bind_clang_isStatement<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isStatement

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isAttribute
template <class Pybind11T> struct Bind_clang_isAttribute : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isAttribute(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isAttribute",
        static_cast<unsigned int (*)(CXCursorKind)>(&clang_isAttribute),
        R"_pb11_weaver(/**
 * Determine whether the given cursor kind represents an attribute.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isAttribute"; }
};

struct Entity_clang_isAttribute
    : public Bind_clang_isAttribute<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isAttribute(EntityScope parent_h)
      : Bind_clang_isAttribute<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isAttribute

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_hasAttrs
template <class Pybind11T>
struct Bind_clang_Cursor_hasAttrs : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_hasAttrs(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_hasAttrs",
        static_cast<unsigned int (*)(CXCursor)>(&clang_Cursor_hasAttrs),
        R"_pb11_weaver(/**
 * Determine whether the given cursor has any attributes.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_hasAttrs"; }
};

struct Entity_clang_Cursor_hasAttrs
    : public Bind_clang_Cursor_hasAttrs<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_hasAttrs(EntityScope parent_h)
      : Bind_clang_Cursor_hasAttrs<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_hasAttrs

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isInvalid
template <class Pybind11T> struct Bind_clang_isInvalid : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isInvalid(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_isInvalid",
                 static_cast<unsigned int (*)(CXCursorKind)>(&clang_isInvalid),
                 R"_pb11_weaver(/**
 * Determine whether the given cursor kind represents an invalid
 * cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isInvalid"; }
};

struct Entity_clang_isInvalid
    : public Bind_clang_isInvalid<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isInvalid(EntityScope parent_h)
      : Bind_clang_isInvalid<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isInvalid

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isTranslationUnit
template <class Pybind11T>
struct Bind_clang_isTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isTranslationUnit",
        static_cast<unsigned int (*)(CXCursorKind)>(&clang_isTranslationUnit),
        R"_pb11_weaver(/**
 * Determine whether the given cursor kind represents a translation
 * unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isTranslationUnit"; }
};

struct Entity_clang_isTranslationUnit
    : public Bind_clang_isTranslationUnit<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isTranslationUnit(EntityScope parent_h)
      : Bind_clang_isTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isPreprocessing
template <class Pybind11T>
struct Bind_clang_isPreprocessing : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isPreprocessing(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isPreprocessing",
        static_cast<unsigned int (*)(CXCursorKind)>(&clang_isPreprocessing),
        R"_pb11_weaver(/***
 * Determine whether the given cursor represents a preprocessing
 * element, such as a preprocessor directive or macro instantiation.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isPreprocessing"; }
};

struct Entity_clang_isPreprocessing
    : public Bind_clang_isPreprocessing<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isPreprocessing(EntityScope parent_h)
      : Bind_clang_isPreprocessing<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isPreprocessing

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isUnexposed
template <class Pybind11T> struct Bind_clang_isUnexposed : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isUnexposed(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isUnexposed",
        static_cast<unsigned int (*)(CXCursorKind)>(&clang_isUnexposed),
        R"_pb11_weaver(/***
 * Determine whether the given cursor represents a currently
 *  unexposed piece of the AST (e.g., CXCursor_UnexposedStmt).
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isUnexposed"; }
};

struct Entity_clang_isUnexposed
    : public Bind_clang_isUnexposed<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isUnexposed(EntityScope parent_h)
      : Bind_clang_isUnexposed<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isUnexposed

#ifndef PB11_WEAVER_DISABLE_Entity_CXLinkageKind
template <class Pybind11T> struct Bind_CXLinkageKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXLinkageKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value(
        "CXLinkage_Invalid", CXLinkageKind::CXLinkage_Invalid,
        R"_pb11_weaver(/** This value indicates that no linkage information is available
   * for a provided CXCursor. */)_pb11_weaver");
    pb11_obj.value("CXLinkage_NoLinkage", CXLinkageKind::CXLinkage_NoLinkage,
                   R"_pb11_weaver(/**
   * This is the linkage for variables, parameters, and so on that
   *  have automatic storage.  This covers normal (non-extern) local variables.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXLinkage_Internal", CXLinkageKind::CXLinkage_Internal,
        R"_pb11_weaver(/** This is the linkage for static variables and static functions. */)_pb11_weaver");
    pb11_obj.value(
        "CXLinkage_UniqueExternal", CXLinkageKind::CXLinkage_UniqueExternal,
        R"_pb11_weaver(/** This is the linkage for entities with external linkage that live
   * in C++ anonymous namespaces.*/)_pb11_weaver");
    pb11_obj.value(
        "CXLinkage_External", CXLinkageKind::CXLinkage_External,
        R"_pb11_weaver(/** This is the linkage for entities with true, external linkage. */)_pb11_weaver");
  }

  static const char *Key() { return "CXLinkageKind"; }
};

struct Entity_CXLinkageKind
    : public Bind_CXLinkageKind<std::decay_t<pybind11::enum_<CXLinkageKind>>> {

  explicit Entity_CXLinkageKind(EntityScope parent_h)
      : Bind_CXLinkageKind<std::decay_t<pybind11::enum_<CXLinkageKind>>>(
            parent_h),
        handle{parent_h, "CXLinkageKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describe the linkage of the entity referred to by a cursor.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXLinkageKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXLinkageKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorLinkage
template <class Pybind11T>
struct Bind_clang_getCursorLinkage : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorLinkage(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorLinkage",
        static_cast<CXLinkageKind (*)(CXCursor)>(&clang_getCursorLinkage),
        R"_pb11_weaver(/**
 * Determine the linkage of the entity referred to by a given cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorLinkage"; }
};

struct Entity_clang_getCursorLinkage
    : public Bind_clang_getCursorLinkage<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorLinkage(EntityScope parent_h)
      : Bind_clang_getCursorLinkage<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorLinkage

#ifndef PB11_WEAVER_DISABLE_Entity_CXVisibilityKind
template <class Pybind11T> struct Bind_CXVisibilityKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXVisibilityKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value(
        "CXVisibility_Invalid", CXVisibilityKind::CXVisibility_Invalid,
        R"_pb11_weaver(/** This value indicates that no visibility information is available
   * for a provided CXCursor. */)_pb11_weaver");
    pb11_obj.value(
        "CXVisibility_Hidden", CXVisibilityKind::CXVisibility_Hidden,
        R"_pb11_weaver(/** Symbol not seen by the linker. */)_pb11_weaver");
    pb11_obj.value(
        "CXVisibility_Protected", CXVisibilityKind::CXVisibility_Protected,
        R"_pb11_weaver(/** Symbol seen by the linker but resolves to a symbol inside this object. */)_pb11_weaver");
    pb11_obj.value(
        "CXVisibility_Default", CXVisibilityKind::CXVisibility_Default,
        R"_pb11_weaver(/** Symbol seen by the linker and acts like a normal symbol. */)_pb11_weaver");
  }

  static const char *Key() { return "CXVisibilityKind"; }
};

struct Entity_CXVisibilityKind
    : public Bind_CXVisibilityKind<
          std::decay_t<pybind11::enum_<CXVisibilityKind>>> {

  explicit Entity_CXVisibilityKind(EntityScope parent_h)
      : Bind_CXVisibilityKind<std::decay_t<pybind11::enum_<CXVisibilityKind>>>(
            parent_h),
        handle{parent_h, "CXVisibilityKind", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXVisibilityKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXVisibilityKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorVisibility
template <class Pybind11T>
struct Bind_clang_getCursorVisibility : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorVisibility(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorVisibility",
        static_cast<CXVisibilityKind (*)(CXCursor)>(&clang_getCursorVisibility),
        R"_pb11_weaver(/**
 * Describe the visibility of the entity referred to by a cursor.
 *
 * This returns the default visibility if not explicitly specified by
 * a visibility attribute. The default visibility may be changed by
 * commandline arguments.
 *
 * \param cursor The cursor to query.
 *
 * \returns The visibility of the cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorVisibility"; }
};

struct Entity_clang_getCursorVisibility
    : public Bind_clang_getCursorVisibility<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorVisibility(EntityScope parent_h)
      : Bind_clang_getCursorVisibility<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorVisibility

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorAvailability
template <class Pybind11T>
struct Bind_clang_getCursorAvailability : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorAvailability(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCursorAvailability",
                 static_cast<CXAvailabilityKind (*)(CXCursor)>(
                     &clang_getCursorAvailability),
                 R"_pb11_weaver(/**
 * Determine the availability of the entity that this cursor refers to,
 * taking the current target platform into account.
 *
 * \param cursor The cursor to query.
 *
 * \returns The availability of the cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorAvailability"; }
};

struct Entity_clang_getCursorAvailability
    : public Bind_clang_getCursorAvailability<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorAvailability(EntityScope parent_h)
      : Bind_clang_getCursorAvailability<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorAvailability

#ifndef PB11_WEAVER_DISABLE_Entity_CXPlatformAvailability
template <class Pybind11T>
struct Bind_CXPlatformAvailability : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXPlatformAvailability(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * An optional message to provide to a user of this API, e.g., to
   * suggest replacement APIs.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("Platform", &CXPlatformAvailability::Platform,
                           R"_pb11_weaver(/**
   * A string that describes the platform for which this structure
   * provides availability information.
   *
   * Possible values are "ios" or "macos".
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Introduced", &CXPlatformAvailability::Introduced,
                           R"_pb11_weaver(/**
   * The version number in which this entity was introduced.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Deprecated", &CXPlatformAvailability::Deprecated,
                           R"_pb11_weaver(/**
   * The version number in which this entity was deprecated (but is
   * still available).
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Obsoleted", &CXPlatformAvailability::Obsoleted,
                           R"_pb11_weaver(/**
   * The version number in which this entity was obsoleted, and therefore
   * is no longer available.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Unavailable", &CXPlatformAvailability::Unavailable,
                           R"_pb11_weaver(/**
   * Whether the entity is unconditionally unavailable on this platform.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("Message", &CXPlatformAvailability::Message,
                           R"_pb11_weaver(/**
   * An optional message to provide to a user of this API, e.g., to
   * suggest replacement APIs.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXPlatformAvailability"; }
};

struct Entity_CXPlatformAvailability
    : public Bind_CXPlatformAvailability<
          std::decay_t<pybind11::class_<CXPlatformAvailability>>> {

  explicit Entity_CXPlatformAvailability(EntityScope parent_h)
      : Bind_CXPlatformAvailability<
            std::decay_t<pybind11::class_<CXPlatformAvailability>>>(parent_h),
        handle{parent_h, "CXPlatformAvailability", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Describes the availability of a given entity on a particular platform, e.g.,
 * a particular class might only be available on Mac OS 10.7 or newer.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXPlatformAvailability> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXPlatformAvailability

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorPlatformAvailability
template <class Pybind11T>
struct Bind_clang_getCursorPlatformAvailability : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorPlatformAvailability(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorPlatformAvailability",
        static_cast<int (*)(CXCursor, int *, CXString *, int *, CXString *,
                            CXPlatformAvailability *, int)>(
            &clang_getCursorPlatformAvailability),
        R"_pb11_weaver(/**
 * Determine the availability of the entity that this cursor refers to
 * on any platforms for which availability information is known.
 *
 * \param cursor The cursor to query.
 *
 * \param always_deprecated If non-NULL, will be set to indicate whether the
 * entity is deprecated on all platforms.
 *
 * \param deprecated_message If non-NULL, will be set to the message text
 * provided along with the unconditional deprecation of this entity. The client
 * is responsible for deallocating this string.
 *
 * \param always_unavailable If non-NULL, will be set to indicate whether the
 * entity is unavailable on all platforms.
 *
 * \param unavailable_message If non-NULL, will be set to the message text
 * provided along with the unconditional unavailability of this entity. The
 * client is responsible for deallocating this string.
 *
 * \param availability If non-NULL, an array of CXPlatformAvailability instances
 * that will be populated with platform availability information, up to either
 * the number of platforms for which availability information is available (as
 * returned by this function) or \c availability_size, whichever is smaller.
 *
 * \param availability_size The number of elements available in the
 * \c availability array.
 *
 * \returns The number of platforms (N) for which availability information is
 * available (which is unrelated to \c availability_size).
 *
 * Note that the client is responsible for calling
 * \c clang_disposeCXPlatformAvailability to free each of the
 * platform-availability structures returned. There are
 * \c min(N, availability_size) such structures.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorPlatformAvailability"; }
};

struct Entity_clang_getCursorPlatformAvailability
    : public Bind_clang_getCursorPlatformAvailability<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorPlatformAvailability(EntityScope parent_h)
      : Bind_clang_getCursorPlatformAvailability<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorPlatformAvailability

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeCXPlatformAvailability
template <class Pybind11T>
struct Bind_clang_disposeCXPlatformAvailability : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeCXPlatformAvailability(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_disposeCXPlatformAvailability",
                 static_cast<void (*)(CXPlatformAvailability *)>(
                     &clang_disposeCXPlatformAvailability),
                 R"_pb11_weaver(/**
 * Free the memory associated with a \c CXPlatformAvailability structure.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeCXPlatformAvailability"; }
};

struct Entity_clang_disposeCXPlatformAvailability
    : public Bind_clang_disposeCXPlatformAvailability<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeCXPlatformAvailability(EntityScope parent_h)
      : Bind_clang_disposeCXPlatformAvailability<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeCXPlatformAvailability

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getVarDeclInitializer
template <class Pybind11T>
struct Bind_clang_Cursor_getVarDeclInitializer : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getVarDeclInitializer(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getVarDeclInitializer",
                 static_cast<CXCursor (*)(CXCursor)>(
                     &clang_Cursor_getVarDeclInitializer),
                 R"_pb11_weaver(/**
 * If cursor refers to a variable declaration and it has initializer returns
 * cursor referring to the initializer otherwise return null cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getVarDeclInitializer"; }
};

struct Entity_clang_Cursor_getVarDeclInitializer
    : public Bind_clang_Cursor_getVarDeclInitializer<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getVarDeclInitializer(EntityScope parent_h)
      : Bind_clang_Cursor_getVarDeclInitializer<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getVarDeclInitializer

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_hasVarDeclGlobalStorage
template <class Pybind11T>
struct Bind_clang_Cursor_hasVarDeclGlobalStorage : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_hasVarDeclGlobalStorage(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_hasVarDeclGlobalStorage",
        static_cast<int (*)(CXCursor)>(&clang_Cursor_hasVarDeclGlobalStorage),
        R"_pb11_weaver(/**
 * If cursor refers to a variable declaration that has global storage returns 1.
 * If cursor refers to a variable declaration that doesn't have global storage
 * returns 0. Otherwise returns -1.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_hasVarDeclGlobalStorage"; }
};

struct Entity_clang_Cursor_hasVarDeclGlobalStorage
    : public Bind_clang_Cursor_hasVarDeclGlobalStorage<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_hasVarDeclGlobalStorage(EntityScope parent_h)
      : Bind_clang_Cursor_hasVarDeclGlobalStorage<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_hasVarDeclGlobalStorage

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_hasVarDeclExternalStorage
template <class Pybind11T>
struct Bind_clang_Cursor_hasVarDeclExternalStorage : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_hasVarDeclExternalStorage(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_hasVarDeclExternalStorage",
        static_cast<int (*)(CXCursor)>(&clang_Cursor_hasVarDeclExternalStorage),
        R"_pb11_weaver(/**
 * If cursor refers to a variable declaration that has external storage
 * returns 1. If cursor refers to a variable declaration that doesn't have
 * external storage returns 0. Otherwise returns -1.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_hasVarDeclExternalStorage"; }
};

struct Entity_clang_Cursor_hasVarDeclExternalStorage
    : public Bind_clang_Cursor_hasVarDeclExternalStorage<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_hasVarDeclExternalStorage(EntityScope parent_h)
      : Bind_clang_Cursor_hasVarDeclExternalStorage<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_hasVarDeclExternalStorage

#ifndef PB11_WEAVER_DISABLE_Entity_CXLanguageKind
template <class Pybind11T> struct Bind_CXLanguageKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXLanguageKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXLanguage_Invalid", CXLanguageKind::CXLanguage_Invalid);
    pb11_obj.value("CXLanguage_C", CXLanguageKind::CXLanguage_C);
    pb11_obj.value("CXLanguage_ObjC", CXLanguageKind::CXLanguage_ObjC);
    pb11_obj.value("CXLanguage_CPlusPlus",
                   CXLanguageKind::CXLanguage_CPlusPlus);
  }

  static const char *Key() { return "CXLanguageKind"; }
};

struct Entity_CXLanguageKind
    : public Bind_CXLanguageKind<
          std::decay_t<pybind11::enum_<CXLanguageKind>>> {

  explicit Entity_CXLanguageKind(EntityScope parent_h)
      : Bind_CXLanguageKind<std::decay_t<pybind11::enum_<CXLanguageKind>>>(
            parent_h),
        handle{parent_h, "CXLanguageKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describe the "language" of the entity referred to by a cursor.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXLanguageKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXLanguageKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorLanguage
template <class Pybind11T>
struct Bind_clang_getCursorLanguage : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorLanguage(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorLanguage",
        static_cast<CXLanguageKind (*)(CXCursor)>(&clang_getCursorLanguage),
        R"_pb11_weaver(/**
 * Determine the "language" of the entity referred to by a given cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorLanguage"; }
};

struct Entity_clang_getCursorLanguage
    : public Bind_clang_getCursorLanguage<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorLanguage(EntityScope parent_h)
      : Bind_clang_getCursorLanguage<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorLanguage

#ifndef PB11_WEAVER_DISABLE_Entity_CXTLSKind
template <class Pybind11T> struct Bind_CXTLSKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTLSKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXTLS_None", CXTLSKind::CXTLS_None);
    pb11_obj.value("CXTLS_Dynamic", CXTLSKind::CXTLS_Dynamic);
    pb11_obj.value("CXTLS_Static", CXTLSKind::CXTLS_Static);
  }

  static const char *Key() { return "CXTLSKind"; }
};

struct Entity_CXTLSKind
    : public Bind_CXTLSKind<std::decay_t<pybind11::enum_<CXTLSKind>>> {

  explicit Entity_CXTLSKind(EntityScope parent_h)
      : Bind_CXTLSKind<std::decay_t<pybind11::enum_<CXTLSKind>>>(parent_h),
        handle{parent_h, "CXTLSKind", pybind11::arithmetic(), R"_pb11_weaver(/**
 * Describe the "thread-local storage (TLS) kind" of the declaration
 * referred to by a cursor.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXTLSKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTLSKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorTLSKind
template <class Pybind11T>
struct Bind_clang_getCursorTLSKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorTLSKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCursorTLSKind",
                 static_cast<CXTLSKind (*)(CXCursor)>(&clang_getCursorTLSKind),
                 R"_pb11_weaver(/**
 * Determine the "thread-local storage (TLS) kind" of the declaration
 * referred to by a cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorTLSKind"; }
};

struct Entity_clang_getCursorTLSKind
    : public Bind_clang_getCursorTLSKind<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorTLSKind(EntityScope parent_h)
      : Bind_clang_getCursorTLSKind<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorTLSKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTranslationUnit
template <class Pybind11T>
struct Bind_clang_Cursor_getTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getTranslationUnit",
        [=](CXCursor arg0) {
          return pybind11_weaver::WrapP<CXTranslationUnitImpl *>(
                     clang_Cursor_getTranslationUnit(arg0))
              .release();
        },
        R"_pb11_weaver(/**
 * Returns the translation unit that a cursor originated from.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getTranslationUnit"; }
};

struct Entity_clang_Cursor_getTranslationUnit
    : public Bind_clang_Cursor_getTranslationUnit<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getTranslationUnit(EntityScope parent_h)
      : Bind_clang_Cursor_getTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_clang_createCXCursorSet
template <class Pybind11T>
struct Bind_clang_createCXCursorSet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_createCXCursorSet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_createCXCursorSet",
        [=]() {
          return pybind11_weaver::WrapP<CXCursorSetImpl *>(
                     clang_createCXCursorSet())
              .release();
        },
        R"_pb11_weaver(/**
 * Creates an empty CXCursorSet.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_createCXCursorSet"; }
};

struct Entity_clang_createCXCursorSet
    : public Bind_clang_createCXCursorSet<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_createCXCursorSet(EntityScope parent_h)
      : Bind_clang_createCXCursorSet<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_createCXCursorSet

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeCXCursorSet
template <class Pybind11T>
struct Bind_clang_disposeCXCursorSet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeCXCursorSet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_disposeCXCursorSet",
        [=](pybind11_weaver::PointerWrapper<CXCursorSetImpl *> *cset) {
          return clang_disposeCXCursorSet((cset)->ptr);
        },
        R"_pb11_weaver(/**
 * Disposes a CXCursorSet and releases its associated memory.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeCXCursorSet"; }
};

struct Entity_clang_disposeCXCursorSet
    : public Bind_clang_disposeCXCursorSet<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeCXCursorSet(EntityScope parent_h)
      : Bind_clang_disposeCXCursorSet<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeCXCursorSet

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXCursorSet_contains
template <class Pybind11T>
struct Bind_clang_CXCursorSet_contains : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXCursorSet_contains(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXCursorSet_contains",
        [=](pybind11_weaver::PointerWrapper<CXCursorSetImpl *> *cset,
            CXCursor cursor) {
          return clang_CXCursorSet_contains((cset)->ptr, cursor);
        },
        R"_pb11_weaver(/**
 * Queries a CXCursorSet to see if it contains a specific CXCursor.
 *
 * \returns non-zero if the set contains the specified cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXCursorSet_contains"; }
};

struct Entity_clang_CXCursorSet_contains
    : public Bind_clang_CXCursorSet_contains<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXCursorSet_contains(EntityScope parent_h)
      : Bind_clang_CXCursorSet_contains<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXCursorSet_contains

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXCursorSet_insert
template <class Pybind11T>
struct Bind_clang_CXCursorSet_insert : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXCursorSet_insert(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXCursorSet_insert",
        [=](pybind11_weaver::PointerWrapper<CXCursorSetImpl *> *cset,
            CXCursor cursor) {
          return clang_CXCursorSet_insert((cset)->ptr, cursor);
        },
        R"_pb11_weaver(/**
 * Inserts a CXCursor into a CXCursorSet.
 *
 * \returns zero if the CXCursor was already in the set, and non-zero otherwise.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXCursorSet_insert"; }
};

struct Entity_clang_CXCursorSet_insert
    : public Bind_clang_CXCursorSet_insert<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXCursorSet_insert(EntityScope parent_h)
      : Bind_clang_CXCursorSet_insert<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXCursorSet_insert

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorSemanticParent
template <class Pybind11T>
struct Bind_clang_getCursorSemanticParent : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorSemanticParent(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorSemanticParent",
        static_cast<CXCursor (*)(CXCursor)>(&clang_getCursorSemanticParent),
        R"_pb11_weaver(/**
 * Determine the semantic parent of the given cursor.
 *
 * The semantic parent of a cursor is the cursor that semantically contains
 * the given \p cursor. For many declarations, the lexical and semantic parents
 * are equivalent (the lexical parent is returned by
 * \c clang_getCursorLexicalParent()). They diverge when declarations or
 * definitions are provided out-of-line. For example:
 *
 * \code
 * class C {
 *  void f();
 * };
 *
 * void C::f() { }
 * \endcode
 *
 * In the out-of-line definition of \c C::f, the semantic parent is
 * the class \c C, of which this function is a member. The lexical parent is
 * the place where the declaration actually occurs in the source code; in this
 * case, the definition occurs in the translation unit. In general, the
 * lexical parent for a given entity can change without affecting the semantics
 * of the program, and the lexical parent of different declarations of the
 * same entity may be different. Changing the semantic parent of a declaration,
 * on the other hand, can have a major impact on semantics, and redeclarations
 * of a particular entity should all have the same semantic context.
 *
 * In the example above, both declarations of \c C::f have \c C as their
 * semantic context, while the lexical context of the first \c C::f is \c C
 * and the lexical context of the second \c C::f is the translation unit.
 *
 * For global declarations, the semantic parent is the translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorSemanticParent"; }
};

struct Entity_clang_getCursorSemanticParent
    : public Bind_clang_getCursorSemanticParent<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorSemanticParent(EntityScope parent_h)
      : Bind_clang_getCursorSemanticParent<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorSemanticParent

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorLexicalParent
template <class Pybind11T>
struct Bind_clang_getCursorLexicalParent : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorLexicalParent(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorLexicalParent",
        static_cast<CXCursor (*)(CXCursor)>(&clang_getCursorLexicalParent),
        R"_pb11_weaver(/**
 * Determine the lexical parent of the given cursor.
 *
 * The lexical parent of a cursor is the cursor in which the given \p cursor
 * was actually written. For many declarations, the lexical and semantic parents
 * are equivalent (the semantic parent is returned by
 * \c clang_getCursorSemanticParent()). They diverge when declarations or
 * definitions are provided out-of-line. For example:
 *
 * \code
 * class C {
 *  void f();
 * };
 *
 * void C::f() { }
 * \endcode
 *
 * In the out-of-line definition of \c C::f, the semantic parent is
 * the class \c C, of which this function is a member. The lexical parent is
 * the place where the declaration actually occurs in the source code; in this
 * case, the definition occurs in the translation unit. In general, the
 * lexical parent for a given entity can change without affecting the semantics
 * of the program, and the lexical parent of different declarations of the
 * same entity may be different. Changing the semantic parent of a declaration,
 * on the other hand, can have a major impact on semantics, and redeclarations
 * of a particular entity should all have the same semantic context.
 *
 * In the example above, both declarations of \c C::f have \c C as their
 * semantic context, while the lexical context of the first \c C::f is \c C
 * and the lexical context of the second \c C::f is the translation unit.
 *
 * For declarations written in the global scope, the lexical parent is
 * the translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorLexicalParent"; }
};

struct Entity_clang_getCursorLexicalParent
    : public Bind_clang_getCursorLexicalParent<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorLexicalParent(EntityScope parent_h)
      : Bind_clang_getCursorLexicalParent<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorLexicalParent

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getOverriddenCursors
template <class Pybind11T>
struct Bind_clang_getOverriddenCursors : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getOverriddenCursors(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getOverriddenCursors",
        [=](CXCursor cursor,
            pybind11_weaver::PointerWrapper<CXCursor **> *overridden,
            unsigned int *num_overridden) {
          return clang_getOverriddenCursors(cursor, (overridden)->ptr,
                                            num_overridden);
        },
        R"_pb11_weaver(/**
 * Determine the set of methods that are overridden by the given
 * method.
 *
 * In both Objective-C and C++, a method (aka virtual member function,
 * in C++) can override a virtual method in a base class. For
 * Objective-C, a method is said to override any method in the class's
 * base class, its protocols, or its categories' protocols, that has the same
 * selector and is of the same kind (class or instance).
 * If no such method exists, the search continues to the class's superclass,
 * its protocols, and its categories, and so on. A method from an Objective-C
 * implementation is considered to override the same methods as its
 * corresponding method in the interface.
 *
 * For C++, a virtual member function overrides any virtual member
 * function with the same signature that occurs in its base
 * classes. With multiple inheritance, a virtual member function can
 * override several virtual member functions coming from different
 * base classes.
 *
 * In all cases, this function determines the immediate overridden
 * method, rather than all of the overridden methods. For example, if
 * a method is originally declared in a class A, then overridden in B
 * (which in inherits from A) and also in C (which inherited from B),
 * then the only overridden method returned from this function when
 * invoked on C's method will be B's method. The client may then
 * invoke this function again, given the previously-found overridden
 * methods, to map out the complete method-override set.
 *
 * \param cursor A cursor representing an Objective-C or C++
 * method. This routine will compute the set of methods that this
 * method overrides.
 *
 * \param overridden A pointer whose pointee will be replaced with a
 * pointer to an array of cursors, representing the set of overridden
 * methods. If there are no overridden methods, the pointee will be
 * set to NULL. The pointee must be freed via a call to
 * \c clang_disposeOverriddenCursors().
 *
 * \param num_overridden A pointer to the number of overridden
 * functions, will be set to the number of overridden functions in the
 * array pointed to by \p overridden.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getOverriddenCursors"; }
};

struct Entity_clang_getOverriddenCursors
    : public Bind_clang_getOverriddenCursors<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getOverriddenCursors(EntityScope parent_h)
      : Bind_clang_getOverriddenCursors<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getOverriddenCursors

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeOverriddenCursors
template <class Pybind11T>
struct Bind_clang_disposeOverriddenCursors : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeOverriddenCursors(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_disposeOverriddenCursors",
        static_cast<void (*)(CXCursor *)>(&clang_disposeOverriddenCursors),
        R"_pb11_weaver(/**
 * Free the set of overridden cursors returned by \c
 * clang_getOverriddenCursors().
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeOverriddenCursors"; }
};

struct Entity_clang_disposeOverriddenCursors
    : public Bind_clang_disposeOverriddenCursors<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeOverriddenCursors(EntityScope parent_h)
      : Bind_clang_disposeOverriddenCursors<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeOverriddenCursors

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getIncludedFile
template <class Pybind11T>
struct Bind_clang_getIncludedFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getIncludedFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getIncludedFile",
        [=](CXCursor cursor) {
          return pybind11_weaver::WrapP<void *>(clang_getIncludedFile(cursor))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve the file that is included by the given inclusion directive
 * cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getIncludedFile"; }
};

struct Entity_clang_getIncludedFile
    : public Bind_clang_getIncludedFile<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getIncludedFile(EntityScope parent_h)
      : Bind_clang_getIncludedFile<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getIncludedFile

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursor
template <class Pybind11T> struct Bind_clang_getCursor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursor",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0,
            CXSourceLocation arg1) {
          return clang_getCursor((arg0)->ptr, arg1);
        },
        R"_pb11_weaver(/**
 * Map a source location to the cursor that describes the entity at that
 * location in the source code.
 *
 * clang_getCursor() maps an arbitrary source location within a translation
 * unit down to the most specific cursor that describes the entity at that
 * location. For example, given an expression \c x + y, invoking
 * clang_getCursor() with a source location pointing to "x" will return the
 * cursor for "x"; similarly for "y". If the cursor points anywhere between
 * "x" or "y" (e.g., on the + or the whitespace around it), clang_getCursor()
 * will return a cursor referring to the "+" expression.
 *
 * \returns a cursor representing the entity at the given source location, or
 * a NULL cursor if no such entity can be found.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursor"; }
};

struct Entity_clang_getCursor
    : public Bind_clang_getCursor<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursor(EntityScope parent_h)
      : Bind_clang_getCursor<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorLocation
template <class Pybind11T>
struct Bind_clang_getCursorLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorLocation",
        static_cast<CXSourceLocation (*)(CXCursor)>(&clang_getCursorLocation),
        R"_pb11_weaver(/**
 * Retrieve the physical location of the source constructor referenced
 * by the given cursor.
 *
 * The location of a declaration is typically the location of the name of that
 * declaration, where the name of that declaration would occur if it is
 * unnamed, or some keyword that introduces that particular declaration.
 * The location of a reference is where that reference occurs within the
 * source code.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorLocation"; }
};

struct Entity_clang_getCursorLocation
    : public Bind_clang_getCursorLocation<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorLocation(EntityScope parent_h)
      : Bind_clang_getCursorLocation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorExtent
template <class Pybind11T>
struct Bind_clang_getCursorExtent : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorExtent(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorExtent",
        static_cast<CXSourceRange (*)(CXCursor)>(&clang_getCursorExtent),
        R"_pb11_weaver(/**
 * Retrieve the physical extent of the source construct referenced by
 * the given cursor.
 *
 * The extent of a cursor starts with the file/line/column pointing at the
 * first character within the source construct that the cursor refers to and
 * ends with the last character within that source construct. For a
 * declaration, the extent covers the declaration itself. For a reference,
 * the extent covers the location of the reference (e.g., where the referenced
 * entity was actually used).
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorExtent"; }
};

struct Entity_clang_getCursorExtent
    : public Bind_clang_getCursorExtent<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorExtent(EntityScope parent_h)
      : Bind_clang_getCursorExtent<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorExtent

#ifndef PB11_WEAVER_DISABLE_Entity_CXTypeKind
template <class Pybind11T> struct Bind_CXTypeKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTypeKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXType_Invalid", CXTypeKind::CXType_Invalid,
                   R"_pb11_weaver(/**
   * Represents an invalid type (e.g., where no type is available).
   */)_pb11_weaver");
    pb11_obj.value("CXType_Unexposed", CXTypeKind::CXType_Unexposed,
                   R"_pb11_weaver(/**
   * A type whose specific kind is not exposed via this
   * interface.
   */)_pb11_weaver");
    pb11_obj.value("CXType_Void", CXTypeKind::CXType_Void,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Bool", CXTypeKind::CXType_Bool,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Char_U", CXTypeKind::CXType_Char_U,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_UChar", CXTypeKind::CXType_UChar,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Char16", CXTypeKind::CXType_Char16,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Char32", CXTypeKind::CXType_Char32,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_UShort", CXTypeKind::CXType_UShort,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_UInt", CXTypeKind::CXType_UInt,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ULong", CXTypeKind::CXType_ULong,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ULongLong", CXTypeKind::CXType_ULongLong,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_UInt128", CXTypeKind::CXType_UInt128,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Char_S", CXTypeKind::CXType_Char_S,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_SChar", CXTypeKind::CXType_SChar,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_WChar", CXTypeKind::CXType_WChar,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Short", CXTypeKind::CXType_Short,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Int", CXTypeKind::CXType_Int,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Long", CXTypeKind::CXType_Long,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_LongLong", CXTypeKind::CXType_LongLong,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Int128", CXTypeKind::CXType_Int128,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Float", CXTypeKind::CXType_Float,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Double", CXTypeKind::CXType_Double,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_LongDouble", CXTypeKind::CXType_LongDouble,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_NullPtr", CXTypeKind::CXType_NullPtr,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Overload", CXTypeKind::CXType_Overload,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Dependent", CXTypeKind::CXType_Dependent,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ObjCId", CXTypeKind::CXType_ObjCId,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ObjCClass", CXTypeKind::CXType_ObjCClass,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ObjCSel", CXTypeKind::CXType_ObjCSel,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Float128", CXTypeKind::CXType_Float128,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Half", CXTypeKind::CXType_Half,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Float16", CXTypeKind::CXType_Float16,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ShortAccum", CXTypeKind::CXType_ShortAccum,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Accum", CXTypeKind::CXType_Accum,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_LongAccum", CXTypeKind::CXType_LongAccum,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_UShortAccum", CXTypeKind::CXType_UShortAccum,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_UAccum", CXTypeKind::CXType_UAccum,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ULongAccum", CXTypeKind::CXType_ULongAccum,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_BFloat16", CXTypeKind::CXType_BFloat16,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Ibm128", CXTypeKind::CXType_Ibm128,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_FirstBuiltin", CXTypeKind::CXType_FirstBuiltin,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_LastBuiltin", CXTypeKind::CXType_LastBuiltin,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Complex", CXTypeKind::CXType_Complex,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Pointer", CXTypeKind::CXType_Pointer,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_BlockPointer", CXTypeKind::CXType_BlockPointer,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_LValueReference", CXTypeKind::CXType_LValueReference,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_RValueReference", CXTypeKind::CXType_RValueReference,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Record", CXTypeKind::CXType_Record,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Enum", CXTypeKind::CXType_Enum,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Typedef", CXTypeKind::CXType_Typedef,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ObjCInterface", CXTypeKind::CXType_ObjCInterface,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ObjCObjectPointer",
                   CXTypeKind::CXType_ObjCObjectPointer,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_FunctionNoProto", CXTypeKind::CXType_FunctionNoProto,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_FunctionProto", CXTypeKind::CXType_FunctionProto,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_ConstantArray", CXTypeKind::CXType_ConstantArray,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Vector", CXTypeKind::CXType_Vector,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_IncompleteArray", CXTypeKind::CXType_IncompleteArray,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_VariableArray", CXTypeKind::CXType_VariableArray,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_DependentSizedArray",
                   CXTypeKind::CXType_DependentSizedArray,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_MemberPointer", CXTypeKind::CXType_MemberPointer,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Auto", CXTypeKind::CXType_Auto,
                   R"_pb11_weaver(/* Builtin types */)_pb11_weaver");
    pb11_obj.value("CXType_Elaborated", CXTypeKind::CXType_Elaborated,
                   R"_pb11_weaver(/**
   * Represents a type that was referred to using an elaborated type keyword.
   *
   * E.g., struct S, or via a qualified name, e.g., N::M::type, or both.
   */)_pb11_weaver");
    pb11_obj.value("CXType_Pipe", CXTypeKind::CXType_Pipe,
                   R"_pb11_weaver(/* OpenCL PipeType. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dRO", CXTypeKind::CXType_OCLImage1dRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dArrayRO",
                   CXTypeKind::CXType_OCLImage1dArrayRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dBufferRO",
                   CXTypeKind::CXType_OCLImage1dBufferRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dRO", CXTypeKind::CXType_OCLImage2dRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayRO",
                   CXTypeKind::CXType_OCLImage2dArrayRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dDepthRO",
                   CXTypeKind::CXType_OCLImage2dDepthRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayDepthRO",
                   CXTypeKind::CXType_OCLImage2dArrayDepthRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dMSAARO",
                   CXTypeKind::CXType_OCLImage2dMSAARO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayMSAARO",
                   CXTypeKind::CXType_OCLImage2dArrayMSAARO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dMSAADepthRO",
                   CXTypeKind::CXType_OCLImage2dMSAADepthRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayMSAADepthRO",
                   CXTypeKind::CXType_OCLImage2dArrayMSAADepthRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage3dRO", CXTypeKind::CXType_OCLImage3dRO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dWO", CXTypeKind::CXType_OCLImage1dWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dArrayWO",
                   CXTypeKind::CXType_OCLImage1dArrayWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dBufferWO",
                   CXTypeKind::CXType_OCLImage1dBufferWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dWO", CXTypeKind::CXType_OCLImage2dWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayWO",
                   CXTypeKind::CXType_OCLImage2dArrayWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dDepthWO",
                   CXTypeKind::CXType_OCLImage2dDepthWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayDepthWO",
                   CXTypeKind::CXType_OCLImage2dArrayDepthWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dMSAAWO",
                   CXTypeKind::CXType_OCLImage2dMSAAWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayMSAAWO",
                   CXTypeKind::CXType_OCLImage2dArrayMSAAWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dMSAADepthWO",
                   CXTypeKind::CXType_OCLImage2dMSAADepthWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayMSAADepthWO",
                   CXTypeKind::CXType_OCLImage2dArrayMSAADepthWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage3dWO", CXTypeKind::CXType_OCLImage3dWO,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dRW", CXTypeKind::CXType_OCLImage1dRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dArrayRW",
                   CXTypeKind::CXType_OCLImage1dArrayRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage1dBufferRW",
                   CXTypeKind::CXType_OCLImage1dBufferRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dRW", CXTypeKind::CXType_OCLImage2dRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayRW",
                   CXTypeKind::CXType_OCLImage2dArrayRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dDepthRW",
                   CXTypeKind::CXType_OCLImage2dDepthRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayDepthRW",
                   CXTypeKind::CXType_OCLImage2dArrayDepthRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dMSAARW",
                   CXTypeKind::CXType_OCLImage2dMSAARW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayMSAARW",
                   CXTypeKind::CXType_OCLImage2dArrayMSAARW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dMSAADepthRW",
                   CXTypeKind::CXType_OCLImage2dMSAADepthRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage2dArrayMSAADepthRW",
                   CXTypeKind::CXType_OCLImage2dArrayMSAADepthRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLImage3dRW", CXTypeKind::CXType_OCLImage3dRW,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLSampler", CXTypeKind::CXType_OCLSampler,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLEvent", CXTypeKind::CXType_OCLEvent,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLQueue", CXTypeKind::CXType_OCLQueue,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLReserveID", CXTypeKind::CXType_OCLReserveID,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_ObjCObject", CXTypeKind::CXType_ObjCObject,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_ObjCTypeParam", CXTypeKind::CXType_ObjCTypeParam,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_Attributed", CXTypeKind::CXType_Attributed,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCMcePayload",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCMcePayload,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCImePayload",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCImePayload,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCRefPayload",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCRefPayload,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCSicPayload",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCSicPayload,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCMceResult",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCMceResult,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCImeResult",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCImeResult,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCRefResult",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCRefResult,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCSicResult",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCSicResult,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value(
        "CXType_OCLIntelSubgroupAVCImeResultSingleRefStreamout",
        CXTypeKind::CXType_OCLIntelSubgroupAVCImeResultSingleRefStreamout,
        R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value(
        "CXType_OCLIntelSubgroupAVCImeResultDualRefStreamout",
        CXTypeKind::CXType_OCLIntelSubgroupAVCImeResultDualRefStreamout,
        R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCImeSingleRefStreamin",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCImeSingleRefStreamin,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_OCLIntelSubgroupAVCImeDualRefStreamin",
                   CXTypeKind::CXType_OCLIntelSubgroupAVCImeDualRefStreamin,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_ExtVector", CXTypeKind::CXType_ExtVector,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_Atomic", CXTypeKind::CXType_Atomic,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
    pb11_obj.value("CXType_BTFTagAttributed",
                   CXTypeKind::CXType_BTFTagAttributed,
                   R"_pb11_weaver(/* OpenCL builtin types. */)_pb11_weaver");
  }

  static const char *Key() { return "CXTypeKind"; }
};

struct Entity_CXTypeKind
    : public Bind_CXTypeKind<std::decay_t<pybind11::enum_<CXTypeKind>>> {

  explicit Entity_CXTypeKind(EntityScope parent_h)
      : Bind_CXTypeKind<std::decay_t<pybind11::enum_<CXTypeKind>>>(parent_h),
        handle{parent_h, "CXTypeKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the kind of type
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXTypeKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTypeKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXCallingConv
template <class Pybind11T> struct Bind_CXCallingConv : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCallingConv(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCallingConv_Default",
                   CXCallingConv::CXCallingConv_Default);
    pb11_obj.value("CXCallingConv_C", CXCallingConv::CXCallingConv_C);
    pb11_obj.value("CXCallingConv_X86StdCall",
                   CXCallingConv::CXCallingConv_X86StdCall);
    pb11_obj.value("CXCallingConv_X86FastCall",
                   CXCallingConv::CXCallingConv_X86FastCall);
    pb11_obj.value("CXCallingConv_X86ThisCall",
                   CXCallingConv::CXCallingConv_X86ThisCall);
    pb11_obj.value("CXCallingConv_X86Pascal",
                   CXCallingConv::CXCallingConv_X86Pascal);
    pb11_obj.value("CXCallingConv_AAPCS", CXCallingConv::CXCallingConv_AAPCS);
    pb11_obj.value("CXCallingConv_AAPCS_VFP",
                   CXCallingConv::CXCallingConv_AAPCS_VFP);
    pb11_obj.value("CXCallingConv_X86RegCall",
                   CXCallingConv::CXCallingConv_X86RegCall);
    pb11_obj.value("CXCallingConv_IntelOclBicc",
                   CXCallingConv::CXCallingConv_IntelOclBicc);
    pb11_obj.value("CXCallingConv_Win64", CXCallingConv::CXCallingConv_Win64);
    pb11_obj.value(
        "CXCallingConv_X86_64Win64", CXCallingConv::CXCallingConv_X86_64Win64,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_X86_64SysV", CXCallingConv::CXCallingConv_X86_64SysV,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_X86VectorCall",
        CXCallingConv::CXCallingConv_X86VectorCall,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_Swift", CXCallingConv::CXCallingConv_Swift,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_PreserveMost", CXCallingConv::CXCallingConv_PreserveMost,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_PreserveAll", CXCallingConv::CXCallingConv_PreserveAll,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_AArch64VectorCall",
        CXCallingConv::CXCallingConv_AArch64VectorCall,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_SwiftAsync", CXCallingConv::CXCallingConv_SwiftAsync,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_AArch64SVEPCS",
        CXCallingConv::CXCallingConv_AArch64SVEPCS,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_Invalid", CXCallingConv::CXCallingConv_Invalid,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
    pb11_obj.value(
        "CXCallingConv_Unexposed", CXCallingConv::CXCallingConv_Unexposed,
        R"_pb11_weaver(/* Alias for compatibility with older versions of API. */)_pb11_weaver");
  }

  static const char *Key() { return "CXCallingConv"; }
};

struct Entity_CXCallingConv
    : public Bind_CXCallingConv<std::decay_t<pybind11::enum_<CXCallingConv>>> {

  explicit Entity_CXCallingConv(EntityScope parent_h)
      : Bind_CXCallingConv<std::decay_t<pybind11::enum_<CXCallingConv>>>(
            parent_h),
        handle{parent_h, "CXCallingConv", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the calling convention of a function type
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCallingConv> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCallingConv

#ifndef PB11_WEAVER_DISABLE_Entity_CXType
template <class Pybind11T> struct Bind_CXType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("kind", &CXType::kind);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXType"; }
};

struct Entity_CXType
    : public Bind_CXType<std::decay_t<pybind11::class_<CXType>>> {

  explicit Entity_CXType(EntityScope parent_h)
      : Bind_CXType<std::decay_t<pybind11::class_<CXType>>>(parent_h),
        handle{parent_h, "CXType", pybind11::dynamic_attr(), R"_pb11_weaver(/**
 * The type of an element in the abstract syntax tree.
 *
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXType> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorType
template <class Pybind11T> struct Bind_clang_getCursorType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCursorType",
                 static_cast<CXType (*)(CXCursor)>(&clang_getCursorType),
                 R"_pb11_weaver(/**
 * Retrieve the type of a CXCursor (if any).
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorType"; }
};

struct Entity_clang_getCursorType
    : public Bind_clang_getCursorType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorType(EntityScope parent_h)
      : Bind_clang_getCursorType<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTypeSpelling
template <class Pybind11T>
struct Bind_clang_getTypeSpelling : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTypeSpelling(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getTypeSpelling",
                 static_cast<CXString (*)(CXType)>(&clang_getTypeSpelling),
                 R"_pb11_weaver(/**
 * Pretty-print the underlying type using the rules of the
 * language of the translation unit from which it came.
 *
 * If the type is invalid, an empty string is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTypeSpelling"; }
};

struct Entity_clang_getTypeSpelling
    : public Bind_clang_getTypeSpelling<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTypeSpelling(EntityScope parent_h)
      : Bind_clang_getTypeSpelling<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTypeSpelling

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTypedefDeclUnderlyingType
template <class Pybind11T>
struct Bind_clang_getTypedefDeclUnderlyingType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTypedefDeclUnderlyingType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTypedefDeclUnderlyingType",
        static_cast<CXType (*)(CXCursor)>(&clang_getTypedefDeclUnderlyingType),
        R"_pb11_weaver(/**
 * Retrieve the underlying type of a typedef declaration.
 *
 * If the cursor does not reference a typedef declaration, an invalid type is
 * returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTypedefDeclUnderlyingType"; }
};

struct Entity_clang_getTypedefDeclUnderlyingType
    : public Bind_clang_getTypedefDeclUnderlyingType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTypedefDeclUnderlyingType(EntityScope parent_h)
      : Bind_clang_getTypedefDeclUnderlyingType<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTypedefDeclUnderlyingType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getEnumDeclIntegerType
template <class Pybind11T>
struct Bind_clang_getEnumDeclIntegerType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getEnumDeclIntegerType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getEnumDeclIntegerType",
        static_cast<CXType (*)(CXCursor)>(&clang_getEnumDeclIntegerType),
        R"_pb11_weaver(/**
 * Retrieve the integer type of an enum declaration.
 *
 * If the cursor does not reference an enum declaration, an invalid type is
 * returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getEnumDeclIntegerType"; }
};

struct Entity_clang_getEnumDeclIntegerType
    : public Bind_clang_getEnumDeclIntegerType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getEnumDeclIntegerType(EntityScope parent_h)
      : Bind_clang_getEnumDeclIntegerType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getEnumDeclIntegerType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getEnumConstantDeclValue
template <class Pybind11T>
struct Bind_clang_getEnumConstantDeclValue : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getEnumConstantDeclValue(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getEnumConstantDeclValue",
        static_cast<long long (*)(CXCursor)>(&clang_getEnumConstantDeclValue),
        R"_pb11_weaver(/**
 * Retrieve the integer value of an enum constant declaration as a signed
 *  long long.
 *
 * If the cursor does not reference an enum constant declaration, LLONG_MIN is
 * returned. Since this is also potentially a valid constant value, the kind of
 * the cursor must be verified before calling this function.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getEnumConstantDeclValue"; }
};

struct Entity_clang_getEnumConstantDeclValue
    : public Bind_clang_getEnumConstantDeclValue<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getEnumConstantDeclValue(EntityScope parent_h)
      : Bind_clang_getEnumConstantDeclValue<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getEnumConstantDeclValue

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getEnumConstantDeclUnsignedValue
template <class Pybind11T>
struct Bind_clang_getEnumConstantDeclUnsignedValue : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getEnumConstantDeclUnsignedValue(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getEnumConstantDeclUnsignedValue",
                 static_cast<unsigned long long (*)(CXCursor)>(
                     &clang_getEnumConstantDeclUnsignedValue),
                 R"_pb11_weaver(/**
 * Retrieve the integer value of an enum constant declaration as an unsigned
 *  long long.
 *
 * If the cursor does not reference an enum constant declaration, ULLONG_MAX is
 * returned. Since this is also potentially a valid constant value, the kind of
 * the cursor must be verified before calling this function.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getEnumConstantDeclUnsignedValue"; }
};

struct Entity_clang_getEnumConstantDeclUnsignedValue
    : public Bind_clang_getEnumConstantDeclUnsignedValue<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getEnumConstantDeclUnsignedValue(EntityScope parent_h)
      : Bind_clang_getEnumConstantDeclUnsignedValue<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getEnumConstantDeclUnsignedValue

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getFieldDeclBitWidth
template <class Pybind11T>
struct Bind_clang_getFieldDeclBitWidth : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getFieldDeclBitWidth(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getFieldDeclBitWidth",
                 static_cast<int (*)(CXCursor)>(&clang_getFieldDeclBitWidth),
                 R"_pb11_weaver(/**
 * Retrieve the bit width of a bit field declaration as an integer.
 *
 * If a cursor that is not a bit field declaration is passed in, -1 is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getFieldDeclBitWidth"; }
};

struct Entity_clang_getFieldDeclBitWidth
    : public Bind_clang_getFieldDeclBitWidth<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getFieldDeclBitWidth(EntityScope parent_h)
      : Bind_clang_getFieldDeclBitWidth<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getFieldDeclBitWidth

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getNumArguments
template <class Pybind11T>
struct Bind_clang_Cursor_getNumArguments : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getNumArguments(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getNumArguments",
                 static_cast<int (*)(CXCursor)>(&clang_Cursor_getNumArguments),
                 R"_pb11_weaver(/**
 * Retrieve the number of non-variadic arguments associated with a given
 * cursor.
 *
 * The number of arguments can be determined for calls as well as for
 * declarations of functions or methods. For other cursors -1 is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getNumArguments"; }
};

struct Entity_clang_Cursor_getNumArguments
    : public Bind_clang_Cursor_getNumArguments<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getNumArguments(EntityScope parent_h)
      : Bind_clang_Cursor_getNumArguments<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getNumArguments

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getArgument
template <class Pybind11T>
struct Bind_clang_Cursor_getArgument : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getArgument(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getArgument",
                 static_cast<CXCursor (*)(CXCursor, unsigned int)>(
                     &clang_Cursor_getArgument),
                 R"_pb11_weaver(/**
 * Retrieve the argument cursor of a function or method.
 *
 * The argument cursor can be determined for calls as well as for declarations
 * of functions or methods. For other cursors and for invalid indices, an
 * invalid cursor is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getArgument"; }
};

struct Entity_clang_Cursor_getArgument
    : public Bind_clang_Cursor_getArgument<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getArgument(EntityScope parent_h)
      : Bind_clang_Cursor_getArgument<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getArgument

#ifndef PB11_WEAVER_DISABLE_Entity_CXTemplateArgumentKind
template <class Pybind11T>
struct Bind_CXTemplateArgumentKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTemplateArgumentKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXTemplateArgumentKind_Null",
                   CXTemplateArgumentKind::CXTemplateArgumentKind_Null);
    pb11_obj.value("CXTemplateArgumentKind_Type",
                   CXTemplateArgumentKind::CXTemplateArgumentKind_Type);
    pb11_obj.value("CXTemplateArgumentKind_Declaration",
                   CXTemplateArgumentKind::CXTemplateArgumentKind_Declaration);
    pb11_obj.value("CXTemplateArgumentKind_NullPtr",
                   CXTemplateArgumentKind::CXTemplateArgumentKind_NullPtr);
    pb11_obj.value("CXTemplateArgumentKind_Integral",
                   CXTemplateArgumentKind::CXTemplateArgumentKind_Integral);
    pb11_obj.value("CXTemplateArgumentKind_Template",
                   CXTemplateArgumentKind::CXTemplateArgumentKind_Template);
    pb11_obj.value(
        "CXTemplateArgumentKind_TemplateExpansion",
        CXTemplateArgumentKind::CXTemplateArgumentKind_TemplateExpansion);
    pb11_obj.value("CXTemplateArgumentKind_Expression",
                   CXTemplateArgumentKind::CXTemplateArgumentKind_Expression);
    pb11_obj.value("CXTemplateArgumentKind_Pack",
                   CXTemplateArgumentKind::CXTemplateArgumentKind_Pack);
    pb11_obj.value(
        "CXTemplateArgumentKind_Invalid",
        CXTemplateArgumentKind::CXTemplateArgumentKind_Invalid,
        R"_pb11_weaver(/* Indicates an error case, preventing the kind from being deduced. */)_pb11_weaver");
  }

  static const char *Key() { return "CXTemplateArgumentKind"; }
};

struct Entity_CXTemplateArgumentKind
    : public Bind_CXTemplateArgumentKind<
          std::decay_t<pybind11::enum_<CXTemplateArgumentKind>>> {

  explicit Entity_CXTemplateArgumentKind(EntityScope parent_h)
      : Bind_CXTemplateArgumentKind<
            std::decay_t<pybind11::enum_<CXTemplateArgumentKind>>>(parent_h),
        handle{parent_h, "CXTemplateArgumentKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the kind of a template argument.
 *
 * See the definition of llvm::clang::TemplateArgument::ArgKind for full
 * element descriptions.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXTemplateArgumentKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTemplateArgumentKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getNumTemplateArguments
template <class Pybind11T>
struct Bind_clang_Cursor_getNumTemplateArguments : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getNumTemplateArguments(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getNumTemplateArguments",
        static_cast<int (*)(CXCursor)>(&clang_Cursor_getNumTemplateArguments),
        R"_pb11_weaver(/**
 * Returns the number of template args of a function, struct, or class decl
 * representing a template specialization.
 *
 * If the argument cursor cannot be converted into a template function
 * declaration, -1 is returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * The value 3 would be returned from this call.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getNumTemplateArguments"; }
};

struct Entity_clang_Cursor_getNumTemplateArguments
    : public Bind_clang_Cursor_getNumTemplateArguments<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getNumTemplateArguments(EntityScope parent_h)
      : Bind_clang_Cursor_getNumTemplateArguments<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getNumTemplateArguments

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTemplateArgumentKind
template <class Pybind11T>
struct Bind_clang_Cursor_getTemplateArgumentKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getTemplateArgumentKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getTemplateArgumentKind",
        static_cast<CXTemplateArgumentKind (*)(CXCursor, unsigned int)>(
            &clang_Cursor_getTemplateArgumentKind),
        R"_pb11_weaver(/**
 * Retrieve the kind of the I'th template argument of the CXCursor C.
 *
 * If the argument CXCursor does not represent a FunctionDecl, StructDecl, or
 * ClassTemplatePartialSpecialization, an invalid template argument kind is
 * returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * For I = 0, 1, and 2, Type, Integral, and Integral will be returned,
 * respectively.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getTemplateArgumentKind"; }
};

struct Entity_clang_Cursor_getTemplateArgumentKind
    : public Bind_clang_Cursor_getTemplateArgumentKind<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getTemplateArgumentKind(EntityScope parent_h)
      : Bind_clang_Cursor_getTemplateArgumentKind<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTemplateArgumentKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTemplateArgumentType
template <class Pybind11T>
struct Bind_clang_Cursor_getTemplateArgumentType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getTemplateArgumentType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getTemplateArgumentType",
                 static_cast<CXType (*)(CXCursor, unsigned int)>(
                     &clang_Cursor_getTemplateArgumentType),
                 R"_pb11_weaver(/**
 * Retrieve a CXType representing the type of a TemplateArgument of a
 *  function decl representing a template specialization.
 *
 * If the argument CXCursor does not represent a FunctionDecl, StructDecl,
 * ClassDecl or ClassTemplatePartialSpecialization whose I'th template argument
 * has a kind of CXTemplateArgKind_Integral, an invalid type is returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * If called with I = 0, "float", will be returned.
 * Invalid types will be returned for I == 1 or 2.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getTemplateArgumentType"; }
};

struct Entity_clang_Cursor_getTemplateArgumentType
    : public Bind_clang_Cursor_getTemplateArgumentType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getTemplateArgumentType(EntityScope parent_h)
      : Bind_clang_Cursor_getTemplateArgumentType<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTemplateArgumentType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTemplateArgumentValue
template <class Pybind11T>
struct Bind_clang_Cursor_getTemplateArgumentValue : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getTemplateArgumentValue(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getTemplateArgumentValue",
                 static_cast<long long (*)(CXCursor, unsigned int)>(
                     &clang_Cursor_getTemplateArgumentValue),
                 R"_pb11_weaver(/**
 * Retrieve the value of an Integral TemplateArgument (of a function
 *  decl representing a template specialization) as a signed long long.
 *
 * It is undefined to call this function on a CXCursor that does not represent a
 * FunctionDecl, StructDecl, ClassDecl or ClassTemplatePartialSpecialization
 * whose I'th template argument is not an integral value.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * If called with I = 1 or 2, -7 or true will be returned, respectively.
 * For I == 0, this function's behavior is undefined.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getTemplateArgumentValue"; }
};

struct Entity_clang_Cursor_getTemplateArgumentValue
    : public Bind_clang_Cursor_getTemplateArgumentValue<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getTemplateArgumentValue(EntityScope parent_h)
      : Bind_clang_Cursor_getTemplateArgumentValue<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTemplateArgumentValue

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTemplateArgumentUnsignedValue
template <class Pybind11T>
struct Bind_clang_Cursor_getTemplateArgumentUnsignedValue : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getTemplateArgumentUnsignedValue(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getTemplateArgumentUnsignedValue",
                 static_cast<unsigned long long (*)(CXCursor, unsigned int)>(
                     &clang_Cursor_getTemplateArgumentUnsignedValue),
                 R"_pb11_weaver(/**
 * Retrieve the value of an Integral TemplateArgument (of a function
 *  decl representing a template specialization) as an unsigned long long.
 *
 * It is undefined to call this function on a CXCursor that does not represent a
 * FunctionDecl, StructDecl, ClassDecl or ClassTemplatePartialSpecialization or
 * whose I'th template argument is not an integral value.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, 2147483649, true>();
 *
 * If called with I = 1 or 2, 2147483649 or true will be returned, respectively.
 * For I == 0, this function's behavior is undefined.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_Cursor_getTemplateArgumentUnsignedValue";
  }
};

struct Entity_clang_Cursor_getTemplateArgumentUnsignedValue
    : public Bind_clang_Cursor_getTemplateArgumentUnsignedValue<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getTemplateArgumentUnsignedValue(
      EntityScope parent_h)
      : Bind_clang_Cursor_getTemplateArgumentUnsignedValue<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getTemplateArgumentUnsignedValue

#ifndef PB11_WEAVER_DISABLE_Entity_clang_equalTypes
template <class Pybind11T> struct Bind_clang_equalTypes : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_equalTypes(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_equalTypes",
        static_cast<unsigned int (*)(CXType, CXType)>(&clang_equalTypes),
        R"_pb11_weaver(/**
 * Determine whether two CXTypes represent the same type.
 *
 * \returns non-zero if the CXTypes represent the same type and
 *          zero otherwise.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_equalTypes"; }
};

struct Entity_clang_equalTypes
    : public Bind_clang_equalTypes<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_equalTypes(EntityScope parent_h)
      : Bind_clang_equalTypes<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_equalTypes

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCanonicalType
template <class Pybind11T>
struct Bind_clang_getCanonicalType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCanonicalType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCanonicalType",
                 static_cast<CXType (*)(CXType)>(&clang_getCanonicalType),
                 R"_pb11_weaver(/**
 * Return the canonical type for a CXType.
 *
 * Clang's type system explicitly models typedefs and all the ways
 * a specific type can be represented.  The canonical type is the underlying
 * type with all the "sugar" removed.  For example, if 'T' is a typedef
 * for 'int', the canonical type for 'T' would be 'int'.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCanonicalType"; }
};

struct Entity_clang_getCanonicalType
    : public Bind_clang_getCanonicalType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCanonicalType(EntityScope parent_h)
      : Bind_clang_getCanonicalType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCanonicalType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isConstQualifiedType
template <class Pybind11T>
struct Bind_clang_isConstQualifiedType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isConstQualifiedType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isConstQualifiedType",
        static_cast<unsigned int (*)(CXType)>(&clang_isConstQualifiedType),
        R"_pb11_weaver(/**
 * Determine whether a CXType has the "const" qualifier set,
 * without looking through typedefs that may have added "const" at a
 * different level.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isConstQualifiedType"; }
};

struct Entity_clang_isConstQualifiedType
    : public Bind_clang_isConstQualifiedType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isConstQualifiedType(EntityScope parent_h)
      : Bind_clang_isConstQualifiedType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isConstQualifiedType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isMacroFunctionLike
template <class Pybind11T>
struct Bind_clang_Cursor_isMacroFunctionLike : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isMacroFunctionLike(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_isMacroFunctionLike",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_Cursor_isMacroFunctionLike),
                 R"_pb11_weaver(/**
 * Determine whether a  CXCursor that is a macro, is
 * function like.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isMacroFunctionLike"; }
};

struct Entity_clang_Cursor_isMacroFunctionLike
    : public Bind_clang_Cursor_isMacroFunctionLike<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isMacroFunctionLike(EntityScope parent_h)
      : Bind_clang_Cursor_isMacroFunctionLike<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isMacroFunctionLike

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isMacroBuiltin
template <class Pybind11T>
struct Bind_clang_Cursor_isMacroBuiltin : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isMacroBuiltin(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_isMacroBuiltin",
        static_cast<unsigned int (*)(CXCursor)>(&clang_Cursor_isMacroBuiltin),
        R"_pb11_weaver(/**
 * Determine whether a  CXCursor that is a macro, is a
 * builtin one.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isMacroBuiltin"; }
};

struct Entity_clang_Cursor_isMacroBuiltin
    : public Bind_clang_Cursor_isMacroBuiltin<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isMacroBuiltin(EntityScope parent_h)
      : Bind_clang_Cursor_isMacroBuiltin<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isMacroBuiltin

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isFunctionInlined
template <class Pybind11T>
struct Bind_clang_Cursor_isFunctionInlined : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isFunctionInlined(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_isFunctionInlined",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_Cursor_isFunctionInlined),
                 R"_pb11_weaver(/**
 * Determine whether a  CXCursor that is a function declaration, is an
 * inline declaration.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isFunctionInlined"; }
};

struct Entity_clang_Cursor_isFunctionInlined
    : public Bind_clang_Cursor_isFunctionInlined<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isFunctionInlined(EntityScope parent_h)
      : Bind_clang_Cursor_isFunctionInlined<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isFunctionInlined

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isVolatileQualifiedType
template <class Pybind11T>
struct Bind_clang_isVolatileQualifiedType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isVolatileQualifiedType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isVolatileQualifiedType",
        static_cast<unsigned int (*)(CXType)>(&clang_isVolatileQualifiedType),
        R"_pb11_weaver(/**
 * Determine whether a CXType has the "volatile" qualifier set,
 * without looking through typedefs that may have added "volatile" at
 * a different level.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isVolatileQualifiedType"; }
};

struct Entity_clang_isVolatileQualifiedType
    : public Bind_clang_isVolatileQualifiedType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isVolatileQualifiedType(EntityScope parent_h)
      : Bind_clang_isVolatileQualifiedType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isVolatileQualifiedType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isRestrictQualifiedType
template <class Pybind11T>
struct Bind_clang_isRestrictQualifiedType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isRestrictQualifiedType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isRestrictQualifiedType",
        static_cast<unsigned int (*)(CXType)>(&clang_isRestrictQualifiedType),
        R"_pb11_weaver(/**
 * Determine whether a CXType has the "restrict" qualifier set,
 * without looking through typedefs that may have added "restrict" at a
 * different level.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isRestrictQualifiedType"; }
};

struct Entity_clang_isRestrictQualifiedType
    : public Bind_clang_isRestrictQualifiedType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isRestrictQualifiedType(EntityScope parent_h)
      : Bind_clang_isRestrictQualifiedType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isRestrictQualifiedType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getAddressSpace
template <class Pybind11T>
struct Bind_clang_getAddressSpace : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getAddressSpace(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getAddressSpace",
                 static_cast<unsigned int (*)(CXType)>(&clang_getAddressSpace),
                 R"_pb11_weaver(/**
 * Returns the address space of the given type.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getAddressSpace"; }
};

struct Entity_clang_getAddressSpace
    : public Bind_clang_getAddressSpace<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getAddressSpace(EntityScope parent_h)
      : Bind_clang_getAddressSpace<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getAddressSpace

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTypedefName
template <class Pybind11T>
struct Bind_clang_getTypedefName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTypedefName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getTypedefName",
                 static_cast<CXString (*)(CXType)>(&clang_getTypedefName),
                 R"_pb11_weaver(/**
 * Returns the typedef name of the given type.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTypedefName"; }
};

struct Entity_clang_getTypedefName
    : public Bind_clang_getTypedefName<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTypedefName(EntityScope parent_h)
      : Bind_clang_getTypedefName<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTypedefName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getPointeeType
template <class Pybind11T>
struct Bind_clang_getPointeeType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getPointeeType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getPointeeType",
                 static_cast<CXType (*)(CXType)>(&clang_getPointeeType),
                 R"_pb11_weaver(/**
 * For pointer types, returns the type of the pointee.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getPointeeType"; }
};

struct Entity_clang_getPointeeType
    : public Bind_clang_getPointeeType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getPointeeType(EntityScope parent_h)
      : Bind_clang_getPointeeType<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getPointeeType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getUnqualifiedType
template <class Pybind11T>
struct Bind_clang_getUnqualifiedType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getUnqualifiedType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getUnqualifiedType",
                 static_cast<CXType (*)(CXType)>(&clang_getUnqualifiedType),
                 R"_pb11_weaver(/**
 * Retrieve the unqualified variant of the given type, removing as
 * little sugar as possible.
 *
 * For example, given the following series of typedefs:
 *
 * \code
 * typedef int Integer;
 * typedef const Integer CInteger;
 * typedef CInteger DifferenceType;
 * \endcode
 *
 * Executing \c clang_getUnqualifiedType() on a \c CXType that
 * represents \c DifferenceType, will desugar to a type representing
 * \c Integer, that has no qualifiers.
 *
 * And, executing \c clang_getUnqualifiedType() on the type of the
 * first argument of the following function declaration:
 *
 * \code
 * void foo(const int);
 * \endcode
 *
 * Will return a type representing \c int, removing the \c const
 * qualifier.
 *
 * Sugar over array types is not desugared.
 *
 * A type can be checked for qualifiers with \c
 * clang_isConstQualifiedType(), \c clang_isVolatileQualifiedType()
 * and \c clang_isRestrictQualifiedType().
 *
 * A type that resulted from a call to \c clang_getUnqualifiedType
 * will return \c false for all of the above calls.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getUnqualifiedType"; }
};

struct Entity_clang_getUnqualifiedType
    : public Bind_clang_getUnqualifiedType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getUnqualifiedType(EntityScope parent_h)
      : Bind_clang_getUnqualifiedType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getUnqualifiedType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNonReferenceType
template <class Pybind11T>
struct Bind_clang_getNonReferenceType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNonReferenceType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getNonReferenceType",
                 static_cast<CXType (*)(CXType)>(&clang_getNonReferenceType),
                 R"_pb11_weaver(/**
 * For reference types (e.g., "const int&"), returns the type that the
 * reference refers to (e.g "const int").
 *
 * Otherwise, returns the type itself.
 *
 * A type that has kind \c CXType_LValueReference or
 * \c CXType_RValueReference is a reference type.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNonReferenceType"; }
};

struct Entity_clang_getNonReferenceType
    : public Bind_clang_getNonReferenceType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNonReferenceType(EntityScope parent_h)
      : Bind_clang_getNonReferenceType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNonReferenceType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTypeDeclaration
template <class Pybind11T>
struct Bind_clang_getTypeDeclaration : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTypeDeclaration(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getTypeDeclaration",
                 static_cast<CXCursor (*)(CXType)>(&clang_getTypeDeclaration),
                 R"_pb11_weaver(/**
 * Return the cursor for the declaration of the given type.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTypeDeclaration"; }
};

struct Entity_clang_getTypeDeclaration
    : public Bind_clang_getTypeDeclaration<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTypeDeclaration(EntityScope parent_h)
      : Bind_clang_getTypeDeclaration<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTypeDeclaration

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDeclObjCTypeEncoding
template <class Pybind11T>
struct Bind_clang_getDeclObjCTypeEncoding : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDeclObjCTypeEncoding(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getDeclObjCTypeEncoding",
        static_cast<CXString (*)(CXCursor)>(&clang_getDeclObjCTypeEncoding),
        R"_pb11_weaver(/**
 * Returns the Objective-C type encoding for the specified declaration.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getDeclObjCTypeEncoding"; }
};

struct Entity_clang_getDeclObjCTypeEncoding
    : public Bind_clang_getDeclObjCTypeEncoding<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDeclObjCTypeEncoding(EntityScope parent_h)
      : Bind_clang_getDeclObjCTypeEncoding<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDeclObjCTypeEncoding

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getObjCEncoding
template <class Pybind11T>
struct Bind_clang_Type_getObjCEncoding : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getObjCEncoding(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getObjCEncoding",
                 static_cast<CXString (*)(CXType)>(&clang_Type_getObjCEncoding),
                 R"_pb11_weaver(/**
 * Returns the Objective-C type encoding for the specified CXType.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getObjCEncoding"; }
};

struct Entity_clang_Type_getObjCEncoding
    : public Bind_clang_Type_getObjCEncoding<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getObjCEncoding(EntityScope parent_h)
      : Bind_clang_Type_getObjCEncoding<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getObjCEncoding

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTypeKindSpelling
template <class Pybind11T>
struct Bind_clang_getTypeKindSpelling : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTypeKindSpelling(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTypeKindSpelling",
        static_cast<CXString (*)(CXTypeKind)>(&clang_getTypeKindSpelling),
        R"_pb11_weaver(/**
 * Retrieve the spelling of a given CXTypeKind.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTypeKindSpelling"; }
};

struct Entity_clang_getTypeKindSpelling
    : public Bind_clang_getTypeKindSpelling<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTypeKindSpelling(EntityScope parent_h)
      : Bind_clang_getTypeKindSpelling<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTypeKindSpelling

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getFunctionTypeCallingConv
template <class Pybind11T>
struct Bind_clang_getFunctionTypeCallingConv : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getFunctionTypeCallingConv(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getFunctionTypeCallingConv",
                 static_cast<CXCallingConv (*)(CXType)>(
                     &clang_getFunctionTypeCallingConv),
                 R"_pb11_weaver(/**
 * Retrieve the calling convention associated with a function type.
 *
 * If a non-function type is passed in, CXCallingConv_Invalid is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getFunctionTypeCallingConv"; }
};

struct Entity_clang_getFunctionTypeCallingConv
    : public Bind_clang_getFunctionTypeCallingConv<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getFunctionTypeCallingConv(EntityScope parent_h)
      : Bind_clang_getFunctionTypeCallingConv<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getFunctionTypeCallingConv

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getResultType
template <class Pybind11T> struct Bind_clang_getResultType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getResultType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getResultType",
                 static_cast<CXType (*)(CXType)>(&clang_getResultType),
                 R"_pb11_weaver(/**
 * Retrieve the return type associated with a function type.
 *
 * If a non-function type is passed in, an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getResultType"; }
};

struct Entity_clang_getResultType
    : public Bind_clang_getResultType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getResultType(EntityScope parent_h)
      : Bind_clang_getResultType<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getResultType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getExceptionSpecificationType
template <class Pybind11T>
struct Bind_clang_getExceptionSpecificationType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getExceptionSpecificationType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getExceptionSpecificationType",
        static_cast<int (*)(CXType)>(&clang_getExceptionSpecificationType),
        R"_pb11_weaver(/**
 * Retrieve the exception specification type associated with a function type.
 * This is a value of type CXCursor_ExceptionSpecificationKind.
 *
 * If a non-function type is passed in, an error code of -1 is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getExceptionSpecificationType"; }
};

struct Entity_clang_getExceptionSpecificationType
    : public Bind_clang_getExceptionSpecificationType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getExceptionSpecificationType(EntityScope parent_h)
      : Bind_clang_getExceptionSpecificationType<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getExceptionSpecificationType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNumArgTypes
template <class Pybind11T>
struct Bind_clang_getNumArgTypes : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNumArgTypes(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getNumArgTypes",
                 static_cast<int (*)(CXType)>(&clang_getNumArgTypes),
                 R"_pb11_weaver(/**
 * Retrieve the number of non-variadic parameters associated with a
 * function type.
 *
 * If a non-function type is passed in, -1 is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNumArgTypes"; }
};

struct Entity_clang_getNumArgTypes
    : public Bind_clang_getNumArgTypes<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNumArgTypes(EntityScope parent_h)
      : Bind_clang_getNumArgTypes<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNumArgTypes

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getArgType
template <class Pybind11T> struct Bind_clang_getArgType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getArgType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getArgType",
        static_cast<CXType (*)(CXType, unsigned int)>(&clang_getArgType),
        R"_pb11_weaver(/**
 * Retrieve the type of a parameter of a function type.
 *
 * If a non-function type is passed in or the function does not have enough
 * parameters, an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getArgType"; }
};

struct Entity_clang_getArgType
    : public Bind_clang_getArgType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getArgType(EntityScope parent_h)
      : Bind_clang_getArgType<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getArgType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getObjCObjectBaseType
template <class Pybind11T>
struct Bind_clang_Type_getObjCObjectBaseType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getObjCObjectBaseType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Type_getObjCObjectBaseType",
        static_cast<CXType (*)(CXType)>(&clang_Type_getObjCObjectBaseType),
        R"_pb11_weaver(/**
 * Retrieves the base type of the ObjCObjectType.
 *
 * If the type is not an ObjC object, an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getObjCObjectBaseType"; }
};

struct Entity_clang_Type_getObjCObjectBaseType
    : public Bind_clang_Type_getObjCObjectBaseType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getObjCObjectBaseType(EntityScope parent_h)
      : Bind_clang_Type_getObjCObjectBaseType<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getObjCObjectBaseType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getNumObjCProtocolRefs
template <class Pybind11T>
struct Bind_clang_Type_getNumObjCProtocolRefs : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getNumObjCProtocolRefs(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getNumObjCProtocolRefs",
                 static_cast<unsigned int (*)(CXType)>(
                     &clang_Type_getNumObjCProtocolRefs),
                 R"_pb11_weaver(/**
 * Retrieve the number of protocol references associated with an ObjC object/id.
 *
 * If the type is not an ObjC object, 0 is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getNumObjCProtocolRefs"; }
};

struct Entity_clang_Type_getNumObjCProtocolRefs
    : public Bind_clang_Type_getNumObjCProtocolRefs<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getNumObjCProtocolRefs(EntityScope parent_h)
      : Bind_clang_Type_getNumObjCProtocolRefs<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getNumObjCProtocolRefs

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getObjCProtocolDecl
template <class Pybind11T>
struct Bind_clang_Type_getObjCProtocolDecl : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getObjCProtocolDecl(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getObjCProtocolDecl",
                 static_cast<CXCursor (*)(CXType, unsigned int)>(
                     &clang_Type_getObjCProtocolDecl),
                 R"_pb11_weaver(/**
 * Retrieve the decl for a protocol reference for an ObjC object/id.
 *
 * If the type is not an ObjC object or there are not enough protocol
 * references, an invalid cursor is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getObjCProtocolDecl"; }
};

struct Entity_clang_Type_getObjCProtocolDecl
    : public Bind_clang_Type_getObjCProtocolDecl<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getObjCProtocolDecl(EntityScope parent_h)
      : Bind_clang_Type_getObjCProtocolDecl<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getObjCProtocolDecl

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getNumObjCTypeArgs
template <class Pybind11T>
struct Bind_clang_Type_getNumObjCTypeArgs : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getNumObjCTypeArgs(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Type_getNumObjCTypeArgs",
        static_cast<unsigned int (*)(CXType)>(&clang_Type_getNumObjCTypeArgs),
        R"_pb11_weaver(/**
 * Retrieve the number of type arguments associated with an ObjC object.
 *
 * If the type is not an ObjC object, 0 is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getNumObjCTypeArgs"; }
};

struct Entity_clang_Type_getNumObjCTypeArgs
    : public Bind_clang_Type_getNumObjCTypeArgs<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getNumObjCTypeArgs(EntityScope parent_h)
      : Bind_clang_Type_getNumObjCTypeArgs<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getNumObjCTypeArgs

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getObjCTypeArg
template <class Pybind11T>
struct Bind_clang_Type_getObjCTypeArg : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getObjCTypeArg(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getObjCTypeArg",
                 static_cast<CXType (*)(CXType, unsigned int)>(
                     &clang_Type_getObjCTypeArg),
                 R"_pb11_weaver(/**
 * Retrieve a type argument associated with an ObjC object.
 *
 * If the type is not an ObjC or the index is not valid,
 * an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getObjCTypeArg"; }
};

struct Entity_clang_Type_getObjCTypeArg
    : public Bind_clang_Type_getObjCTypeArg<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getObjCTypeArg(EntityScope parent_h)
      : Bind_clang_Type_getObjCTypeArg<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getObjCTypeArg

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isFunctionTypeVariadic
template <class Pybind11T>
struct Bind_clang_isFunctionTypeVariadic : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isFunctionTypeVariadic(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isFunctionTypeVariadic",
        static_cast<unsigned int (*)(CXType)>(&clang_isFunctionTypeVariadic),
        R"_pb11_weaver(/**
 * Return 1 if the CXType is a variadic function type, and 0 otherwise.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isFunctionTypeVariadic"; }
};

struct Entity_clang_isFunctionTypeVariadic
    : public Bind_clang_isFunctionTypeVariadic<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isFunctionTypeVariadic(EntityScope parent_h)
      : Bind_clang_isFunctionTypeVariadic<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isFunctionTypeVariadic

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorResultType
template <class Pybind11T>
struct Bind_clang_getCursorResultType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorResultType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCursorResultType",
                 static_cast<CXType (*)(CXCursor)>(&clang_getCursorResultType),
                 R"_pb11_weaver(/**
 * Retrieve the return type associated with a given cursor.
 *
 * This only returns a valid type if the cursor refers to a function or method.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorResultType"; }
};

struct Entity_clang_getCursorResultType
    : public Bind_clang_getCursorResultType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorResultType(EntityScope parent_h)
      : Bind_clang_getCursorResultType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorResultType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorExceptionSpecificationType
template <class Pybind11T>
struct Bind_clang_getCursorExceptionSpecificationType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorExceptionSpecificationType(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCursorExceptionSpecificationType",
                 static_cast<int (*)(CXCursor)>(
                     &clang_getCursorExceptionSpecificationType),
                 R"_pb11_weaver(/**
 * Retrieve the exception specification type associated with a given cursor.
 * This is a value of type CXCursor_ExceptionSpecificationKind.
 *
 * This only returns a valid result if the cursor refers to a function or
 * method.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_getCursorExceptionSpecificationType";
  }
};

struct Entity_clang_getCursorExceptionSpecificationType
    : public Bind_clang_getCursorExceptionSpecificationType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorExceptionSpecificationType(
      EntityScope parent_h)
      : Bind_clang_getCursorExceptionSpecificationType<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorExceptionSpecificationType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isPODType
template <class Pybind11T> struct Bind_clang_isPODType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isPODType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_isPODType",
                 static_cast<unsigned int (*)(CXType)>(&clang_isPODType),
                 R"_pb11_weaver(/**
 * Return 1 if the CXType is a POD (plain old data) type, and 0
 *  otherwise.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isPODType"; }
};

struct Entity_clang_isPODType
    : public Bind_clang_isPODType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isPODType(EntityScope parent_h)
      : Bind_clang_isPODType<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isPODType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getElementType
template <class Pybind11T>
struct Bind_clang_getElementType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getElementType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getElementType",
                 static_cast<CXType (*)(CXType)>(&clang_getElementType),
                 R"_pb11_weaver(/**
 * Return the element type of an array, complex, or vector type.
 *
 * If a type is passed in that is not an array, complex, or vector type,
 * an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getElementType"; }
};

struct Entity_clang_getElementType
    : public Bind_clang_getElementType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getElementType(EntityScope parent_h)
      : Bind_clang_getElementType<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getElementType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNumElements
template <class Pybind11T>
struct Bind_clang_getNumElements : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNumElements(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getNumElements",
                 static_cast<long long (*)(CXType)>(&clang_getNumElements),
                 R"_pb11_weaver(/**
 * Return the number of elements of an array or vector type.
 *
 * If a type is passed in that is not an array or vector type,
 * -1 is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNumElements"; }
};

struct Entity_clang_getNumElements
    : public Bind_clang_getNumElements<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNumElements(EntityScope parent_h)
      : Bind_clang_getNumElements<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNumElements

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getArrayElementType
template <class Pybind11T>
struct Bind_clang_getArrayElementType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getArrayElementType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getArrayElementType",
                 static_cast<CXType (*)(CXType)>(&clang_getArrayElementType),
                 R"_pb11_weaver(/**
 * Return the element type of an array type.
 *
 * If a non-array type is passed in, an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getArrayElementType"; }
};

struct Entity_clang_getArrayElementType
    : public Bind_clang_getArrayElementType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getArrayElementType(EntityScope parent_h)
      : Bind_clang_getArrayElementType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getArrayElementType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getArraySize
template <class Pybind11T> struct Bind_clang_getArraySize : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getArraySize(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getArraySize",
                 static_cast<long long (*)(CXType)>(&clang_getArraySize),
                 R"_pb11_weaver(/**
 * Return the array size of a constant array.
 *
 * If a non-array type is passed in, -1 is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getArraySize"; }
};

struct Entity_clang_getArraySize
    : public Bind_clang_getArraySize<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getArraySize(EntityScope parent_h)
      : Bind_clang_getArraySize<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getArraySize

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getNamedType
template <class Pybind11T>
struct Bind_clang_Type_getNamedType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getNamedType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getNamedType",
                 static_cast<CXType (*)(CXType)>(&clang_Type_getNamedType),
                 R"_pb11_weaver(/**
 * Retrieve the type named by the qualified-id.
 *
 * If a non-elaborated type is passed in, an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getNamedType"; }
};

struct Entity_clang_Type_getNamedType
    : public Bind_clang_Type_getNamedType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getNamedType(EntityScope parent_h)
      : Bind_clang_Type_getNamedType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getNamedType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_isTransparentTagTypedef
template <class Pybind11T>
struct Bind_clang_Type_isTransparentTagTypedef : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_isTransparentTagTypedef(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_isTransparentTagTypedef",
                 static_cast<unsigned int (*)(CXType)>(
                     &clang_Type_isTransparentTagTypedef),
                 R"_pb11_weaver(/**
 * Determine if a typedef is 'transparent' tag.
 *
 * A typedef is considered 'transparent' if it shares a name and spelling
 * location with its underlying tag type, as is the case with the NS_ENUM macro.
 *
 * \returns non-zero if transparent and zero otherwise.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_isTransparentTagTypedef"; }
};

struct Entity_clang_Type_isTransparentTagTypedef
    : public Bind_clang_Type_isTransparentTagTypedef<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_isTransparentTagTypedef(EntityScope parent_h)
      : Bind_clang_Type_isTransparentTagTypedef<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_isTransparentTagTypedef

#ifndef PB11_WEAVER_DISABLE_Entity_CXTypeNullabilityKind
template <class Pybind11T>
struct Bind_CXTypeNullabilityKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTypeNullabilityKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXTypeNullability_NonNull",
                   CXTypeNullabilityKind::CXTypeNullability_NonNull,
                   R"_pb11_weaver(/**
   * Values of this type can never be null.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeNullability_Nullable",
                   CXTypeNullabilityKind::CXTypeNullability_Nullable,
                   R"_pb11_weaver(/**
   * Values of this type can be null.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeNullability_Unspecified",
                   CXTypeNullabilityKind::CXTypeNullability_Unspecified,
                   R"_pb11_weaver(/**
   * Whether values of this type can be null is (explicitly)
   * unspecified. This captures a (fairly rare) case where we
   * can't conclude anything about the nullability of the type even
   * though it has been considered.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeNullability_Invalid",
                   CXTypeNullabilityKind::CXTypeNullability_Invalid,
                   R"_pb11_weaver(/**
   * Nullability is not applicable to this type.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeNullability_NullableResult",
                   CXTypeNullabilityKind::CXTypeNullability_NullableResult,
                   R"_pb11_weaver(/**
   * Generally behaves like Nullable, except when used in a block parameter that
   * was imported into a swift async method. There, swift will assume that the
   * parameter can get null even if no error occurred. _Nullable parameters are
   * assumed to only get null on error.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXTypeNullabilityKind"; }
};

struct Entity_CXTypeNullabilityKind
    : public Bind_CXTypeNullabilityKind<
          std::decay_t<pybind11::enum_<CXTypeNullabilityKind>>> {

  explicit Entity_CXTypeNullabilityKind(EntityScope parent_h)
      : Bind_CXTypeNullabilityKind<
            std::decay_t<pybind11::enum_<CXTypeNullabilityKind>>>(parent_h),
        handle{parent_h, "CXTypeNullabilityKind", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXTypeNullabilityKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTypeNullabilityKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getNullability
template <class Pybind11T>
struct Bind_clang_Type_getNullability : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getNullability(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getNullability",
                 static_cast<CXTypeNullabilityKind (*)(CXType)>(
                     &clang_Type_getNullability),
                 R"_pb11_weaver(/**
 * Retrieve the nullability kind of a pointer type.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getNullability"; }
};

struct Entity_clang_Type_getNullability
    : public Bind_clang_Type_getNullability<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getNullability(EntityScope parent_h)
      : Bind_clang_Type_getNullability<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getNullability

#ifndef PB11_WEAVER_DISABLE_Entity_CXTypeLayoutError
template <class Pybind11T> struct Bind_CXTypeLayoutError : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTypeLayoutError(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXTypeLayoutError_Invalid",
                   CXTypeLayoutError::CXTypeLayoutError_Invalid,
                   R"_pb11_weaver(/**
   * Type is of kind CXType_Invalid.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeLayoutError_Incomplete",
                   CXTypeLayoutError::CXTypeLayoutError_Incomplete,
                   R"_pb11_weaver(/**
   * The type is an incomplete Type.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeLayoutError_Dependent",
                   CXTypeLayoutError::CXTypeLayoutError_Dependent,
                   R"_pb11_weaver(/**
   * The type is a dependent Type.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeLayoutError_NotConstantSize",
                   CXTypeLayoutError::CXTypeLayoutError_NotConstantSize,
                   R"_pb11_weaver(/**
   * The type is not a constant size type.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeLayoutError_InvalidFieldName",
                   CXTypeLayoutError::CXTypeLayoutError_InvalidFieldName,
                   R"_pb11_weaver(/**
   * The Field name is not valid for this record.
   */)_pb11_weaver");
    pb11_obj.value("CXTypeLayoutError_Undeduced",
                   CXTypeLayoutError::CXTypeLayoutError_Undeduced,
                   R"_pb11_weaver(/**
   * The type is undeduced.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXTypeLayoutError"; }
};

struct Entity_CXTypeLayoutError
    : public Bind_CXTypeLayoutError<
          std::decay_t<pybind11::enum_<CXTypeLayoutError>>> {

  explicit Entity_CXTypeLayoutError(EntityScope parent_h)
      : Bind_CXTypeLayoutError<
            std::decay_t<pybind11::enum_<CXTypeLayoutError>>>(parent_h),
        handle{parent_h, "CXTypeLayoutError", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * List the possible error codes for \c clang_Type_getSizeOf,
 *   \c clang_Type_getAlignOf, \c clang_Type_getOffsetOf and
 *   \c clang_Cursor_getOffsetOf.
 *
 * A value of this enumeration type can be returned if the target type is not
 * a valid argument to sizeof, alignof or offsetof.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXTypeLayoutError> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTypeLayoutError

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getAlignOf
template <class Pybind11T>
struct Bind_clang_Type_getAlignOf : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getAlignOf(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getAlignOf",
                 static_cast<long long (*)(CXType)>(&clang_Type_getAlignOf),
                 R"_pb11_weaver(/**
 * Return the alignment of a type in bytes as per C++[expr.alignof]
 *   standard.
 *
 * If the type declaration is invalid, CXTypeLayoutError_Invalid is returned.
 * If the type declaration is an incomplete type, CXTypeLayoutError_Incomplete
 *   is returned.
 * If the type declaration is a dependent type, CXTypeLayoutError_Dependent is
 *   returned.
 * If the type declaration is not a constant size type,
 *   CXTypeLayoutError_NotConstantSize is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getAlignOf"; }
};

struct Entity_clang_Type_getAlignOf
    : public Bind_clang_Type_getAlignOf<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getAlignOf(EntityScope parent_h)
      : Bind_clang_Type_getAlignOf<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getAlignOf

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getClassType
template <class Pybind11T>
struct Bind_clang_Type_getClassType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getClassType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getClassType",
                 static_cast<CXType (*)(CXType)>(&clang_Type_getClassType),
                 R"_pb11_weaver(/**
 * Return the class type of an member pointer type.
 *
 * If a non-member-pointer type is passed in, an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getClassType"; }
};

struct Entity_clang_Type_getClassType
    : public Bind_clang_Type_getClassType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getClassType(EntityScope parent_h)
      : Bind_clang_Type_getClassType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getClassType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getSizeOf
template <class Pybind11T>
struct Bind_clang_Type_getSizeOf : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getSizeOf(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getSizeOf",
                 static_cast<long long (*)(CXType)>(&clang_Type_getSizeOf),
                 R"_pb11_weaver(/**
 * Return the size of a type in bytes as per C++[expr.sizeof] standard.
 *
 * If the type declaration is invalid, CXTypeLayoutError_Invalid is returned.
 * If the type declaration is an incomplete type, CXTypeLayoutError_Incomplete
 *   is returned.
 * If the type declaration is a dependent type, CXTypeLayoutError_Dependent is
 *   returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getSizeOf"; }
};

struct Entity_clang_Type_getSizeOf
    : public Bind_clang_Type_getSizeOf<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getSizeOf(EntityScope parent_h)
      : Bind_clang_Type_getSizeOf<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getSizeOf

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getOffsetOf
template <class Pybind11T>
struct Bind_clang_Type_getOffsetOf : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getOffsetOf(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getOffsetOf",
                 static_cast<long long (*)(CXType, const char *)>(
                     &clang_Type_getOffsetOf),
                 R"_pb11_weaver(/**
 * Return the offset of a field named S in a record of type T in bits
 *   as it would be returned by __offsetof__ as per C++11[18.2p4]
 *
 * If the cursor is not a record field declaration, CXTypeLayoutError_Invalid
 *   is returned.
 * If the field's type declaration is an incomplete type,
 *   CXTypeLayoutError_Incomplete is returned.
 * If the field's type declaration is a dependent type,
 *   CXTypeLayoutError_Dependent is returned.
 * If the field's name S is not found,
 *   CXTypeLayoutError_InvalidFieldName is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getOffsetOf"; }
};

struct Entity_clang_Type_getOffsetOf
    : public Bind_clang_Type_getOffsetOf<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getOffsetOf(EntityScope parent_h)
      : Bind_clang_Type_getOffsetOf<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getOffsetOf

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getModifiedType
template <class Pybind11T>
struct Bind_clang_Type_getModifiedType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getModifiedType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getModifiedType",
                 static_cast<CXType (*)(CXType)>(&clang_Type_getModifiedType),
                 R"_pb11_weaver(/**
 * Return the type that was modified by this attributed type.
 *
 * If the type is not an attributed type, an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getModifiedType"; }
};

struct Entity_clang_Type_getModifiedType
    : public Bind_clang_Type_getModifiedType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getModifiedType(EntityScope parent_h)
      : Bind_clang_Type_getModifiedType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getModifiedType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getValueType
template <class Pybind11T>
struct Bind_clang_Type_getValueType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getValueType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getValueType",
                 static_cast<CXType (*)(CXType)>(&clang_Type_getValueType),
                 R"_pb11_weaver(/**
 * Gets the type contained by this atomic type.
 *
 * If a non-atomic type is passed in, an invalid type is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getValueType"; }
};

struct Entity_clang_Type_getValueType
    : public Bind_clang_Type_getValueType<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getValueType(EntityScope parent_h)
      : Bind_clang_Type_getValueType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getValueType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getOffsetOfField
template <class Pybind11T>
struct Bind_clang_Cursor_getOffsetOfField : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getOffsetOfField(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getOffsetOfField",
        static_cast<long long (*)(CXCursor)>(&clang_Cursor_getOffsetOfField),
        R"_pb11_weaver(/**
 * Return the offset of the field represented by the Cursor.
 *
 * If the cursor is not a field declaration, -1 is returned.
 * If the cursor semantic parent is not a record field declaration,
 *   CXTypeLayoutError_Invalid is returned.
 * If the field's type declaration is an incomplete type,
 *   CXTypeLayoutError_Incomplete is returned.
 * If the field's type declaration is a dependent type,
 *   CXTypeLayoutError_Dependent is returned.
 * If the field's name S is not found,
 *   CXTypeLayoutError_InvalidFieldName is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getOffsetOfField"; }
};

struct Entity_clang_Cursor_getOffsetOfField
    : public Bind_clang_Cursor_getOffsetOfField<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getOffsetOfField(EntityScope parent_h)
      : Bind_clang_Cursor_getOffsetOfField<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getOffsetOfField

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isAnonymous
template <class Pybind11T>
struct Bind_clang_Cursor_isAnonymous : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isAnonymous(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_isAnonymous",
        static_cast<unsigned int (*)(CXCursor)>(&clang_Cursor_isAnonymous),
        R"_pb11_weaver(/**
 * Determine whether the given cursor represents an anonymous
 * tag or namespace
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isAnonymous"; }
};

struct Entity_clang_Cursor_isAnonymous
    : public Bind_clang_Cursor_isAnonymous<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isAnonymous(EntityScope parent_h)
      : Bind_clang_Cursor_isAnonymous<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isAnonymous

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isAnonymousRecordDecl
template <class Pybind11T>
struct Bind_clang_Cursor_isAnonymousRecordDecl : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isAnonymousRecordDecl(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_isAnonymousRecordDecl",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_Cursor_isAnonymousRecordDecl),
                 R"_pb11_weaver(/**
 * Determine whether the given cursor represents an anonymous record
 * declaration.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isAnonymousRecordDecl"; }
};

struct Entity_clang_Cursor_isAnonymousRecordDecl
    : public Bind_clang_Cursor_isAnonymousRecordDecl<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isAnonymousRecordDecl(EntityScope parent_h)
      : Bind_clang_Cursor_isAnonymousRecordDecl<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isAnonymousRecordDecl

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isInlineNamespace
template <class Pybind11T>
struct Bind_clang_Cursor_isInlineNamespace : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isInlineNamespace(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_isInlineNamespace",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_Cursor_isInlineNamespace),
                 R"_pb11_weaver(/**
 * Determine whether the given cursor represents an inline namespace
 * declaration.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isInlineNamespace"; }
};

struct Entity_clang_Cursor_isInlineNamespace
    : public Bind_clang_Cursor_isInlineNamespace<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isInlineNamespace(EntityScope parent_h)
      : Bind_clang_Cursor_isInlineNamespace<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isInlineNamespace

#ifndef PB11_WEAVER_DISABLE_Entity_CXRefQualifierKind
template <class Pybind11T> struct Bind_CXRefQualifierKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXRefQualifierKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value(
        "CXRefQualifier_None", CXRefQualifierKind::CXRefQualifier_None,
        R"_pb11_weaver(/** No ref-qualifier was provided. */)_pb11_weaver");
    pb11_obj.value(
        "CXRefQualifier_LValue", CXRefQualifierKind::CXRefQualifier_LValue,
        R"_pb11_weaver(/** An lvalue ref-qualifier was provided (\c &). */)_pb11_weaver");
    pb11_obj.value(
        "CXRefQualifier_RValue", CXRefQualifierKind::CXRefQualifier_RValue,
        R"_pb11_weaver(/** An rvalue ref-qualifier was provided (\c &&). */)_pb11_weaver");
  }

  static const char *Key() { return "CXRefQualifierKind"; }
};

struct Entity_CXRefQualifierKind
    : public Bind_CXRefQualifierKind<
          std::decay_t<pybind11::enum_<CXRefQualifierKind>>> {

  explicit Entity_CXRefQualifierKind(EntityScope parent_h)
      : Bind_CXRefQualifierKind<
            std::decay_t<pybind11::enum_<CXRefQualifierKind>>>(parent_h),
        handle{parent_h, "CXRefQualifierKind", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXRefQualifierKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXRefQualifierKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getNumTemplateArguments
template <class Pybind11T>
struct Bind_clang_Type_getNumTemplateArguments : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getNumTemplateArguments(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Type_getNumTemplateArguments",
        static_cast<int (*)(CXType)>(&clang_Type_getNumTemplateArguments),
        R"_pb11_weaver(/**
 * Returns the number of template arguments for given template
 * specialization, or -1 if type \c T is not a template specialization.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getNumTemplateArguments"; }
};

struct Entity_clang_Type_getNumTemplateArguments
    : public Bind_clang_Type_getNumTemplateArguments<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getNumTemplateArguments(EntityScope parent_h)
      : Bind_clang_Type_getNumTemplateArguments<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getNumTemplateArguments

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getTemplateArgumentAsType
template <class Pybind11T>
struct Bind_clang_Type_getTemplateArgumentAsType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getTemplateArgumentAsType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getTemplateArgumentAsType",
                 static_cast<CXType (*)(CXType, unsigned int)>(
                     &clang_Type_getTemplateArgumentAsType),
                 R"_pb11_weaver(/**
 * Returns the type template argument of a template class specialization
 * at given index.
 *
 * This function only returns template type arguments and does not handle
 * template template arguments or variadic packs.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getTemplateArgumentAsType"; }
};

struct Entity_clang_Type_getTemplateArgumentAsType
    : public Bind_clang_Type_getTemplateArgumentAsType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getTemplateArgumentAsType(EntityScope parent_h)
      : Bind_clang_Type_getTemplateArgumentAsType<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getTemplateArgumentAsType

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_getCXXRefQualifier
template <class Pybind11T>
struct Bind_clang_Type_getCXXRefQualifier : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_getCXXRefQualifier(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Type_getCXXRefQualifier",
                 static_cast<CXRefQualifierKind (*)(CXType)>(
                     &clang_Type_getCXXRefQualifier),
                 R"_pb11_weaver(/**
 * Retrieve the ref-qualifier kind of a function or method.
 *
 * The ref-qualifier is returned for C++ functions or methods. For other types
 * or non-C++ declarations, CXRefQualifier_None is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_getCXXRefQualifier"; }
};

struct Entity_clang_Type_getCXXRefQualifier
    : public Bind_clang_Type_getCXXRefQualifier<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_getCXXRefQualifier(EntityScope parent_h)
      : Bind_clang_Type_getCXXRefQualifier<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_getCXXRefQualifier

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isBitField
template <class Pybind11T>
struct Bind_clang_Cursor_isBitField : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isBitField(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_isBitField",
        static_cast<unsigned int (*)(CXCursor)>(&clang_Cursor_isBitField),
        R"_pb11_weaver(/**
 * Returns non-zero if the cursor specifies a Record member that is a
 *   bitfield.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isBitField"; }
};

struct Entity_clang_Cursor_isBitField
    : public Bind_clang_Cursor_isBitField<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isBitField(EntityScope parent_h)
      : Bind_clang_Cursor_isBitField<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isBitField

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isVirtualBase
template <class Pybind11T> struct Bind_clang_isVirtualBase : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isVirtualBase(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_isVirtualBase",
                 static_cast<unsigned int (*)(CXCursor)>(&clang_isVirtualBase),
                 R"_pb11_weaver(/**
 * Returns 1 if the base class specified by the cursor with kind
 *   CX_CXXBaseSpecifier is virtual.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isVirtualBase"; }
};

struct Entity_clang_isVirtualBase
    : public Bind_clang_isVirtualBase<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isVirtualBase(EntityScope parent_h)
      : Bind_clang_isVirtualBase<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isVirtualBase

#ifndef PB11_WEAVER_DISABLE_Entity_CX_CXXAccessSpecifier
template <class Pybind11T>
struct Bind_CX_CXXAccessSpecifier : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CX_CXXAccessSpecifier(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CX_CXXInvalidAccessSpecifier",
                   CX_CXXAccessSpecifier::CX_CXXInvalidAccessSpecifier);
    pb11_obj.value("CX_CXXPublic", CX_CXXAccessSpecifier::CX_CXXPublic);
    pb11_obj.value("CX_CXXProtected", CX_CXXAccessSpecifier::CX_CXXProtected);
    pb11_obj.value("CX_CXXPrivate", CX_CXXAccessSpecifier::CX_CXXPrivate);
  }

  static const char *Key() { return "CX_CXXAccessSpecifier"; }
};

struct Entity_CX_CXXAccessSpecifier
    : public Bind_CX_CXXAccessSpecifier<
          std::decay_t<pybind11::enum_<CX_CXXAccessSpecifier>>> {

  explicit Entity_CX_CXXAccessSpecifier(EntityScope parent_h)
      : Bind_CX_CXXAccessSpecifier<
            std::decay_t<pybind11::enum_<CX_CXXAccessSpecifier>>>(parent_h),
        handle{parent_h, "CX_CXXAccessSpecifier", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Represents the C++ access control level to a base class for a
 * cursor with kind CX_CXXBaseSpecifier.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CX_CXXAccessSpecifier> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CX_CXXAccessSpecifier

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCXXAccessSpecifier
template <class Pybind11T>
struct Bind_clang_getCXXAccessSpecifier : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCXXAccessSpecifier(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCXXAccessSpecifier",
                 static_cast<CX_CXXAccessSpecifier (*)(CXCursor)>(
                     &clang_getCXXAccessSpecifier),
                 R"_pb11_weaver(/**
 * Returns the access control level for the referenced object.
 *
 * If the cursor refers to a C++ declaration, its access control level within
 * its parent scope is returned. Otherwise, if the cursor refers to a base
 * specifier or access specifier, the specifier itself is returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCXXAccessSpecifier"; }
};

struct Entity_clang_getCXXAccessSpecifier
    : public Bind_clang_getCXXAccessSpecifier<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCXXAccessSpecifier(EntityScope parent_h)
      : Bind_clang_getCXXAccessSpecifier<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCXXAccessSpecifier

#ifndef PB11_WEAVER_DISABLE_Entity_CX_StorageClass
template <class Pybind11T> struct Bind_CX_StorageClass : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CX_StorageClass(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CX_SC_Invalid", CX_StorageClass::CX_SC_Invalid);
    pb11_obj.value("CX_SC_None", CX_StorageClass::CX_SC_None);
    pb11_obj.value("CX_SC_Extern", CX_StorageClass::CX_SC_Extern);
    pb11_obj.value("CX_SC_Static", CX_StorageClass::CX_SC_Static);
    pb11_obj.value("CX_SC_PrivateExtern", CX_StorageClass::CX_SC_PrivateExtern);
    pb11_obj.value("CX_SC_OpenCLWorkGroupLocal",
                   CX_StorageClass::CX_SC_OpenCLWorkGroupLocal);
    pb11_obj.value("CX_SC_Auto", CX_StorageClass::CX_SC_Auto);
    pb11_obj.value("CX_SC_Register", CX_StorageClass::CX_SC_Register);
  }

  static const char *Key() { return "CX_StorageClass"; }
};

struct Entity_CX_StorageClass
    : public Bind_CX_StorageClass<
          std::decay_t<pybind11::enum_<CX_StorageClass>>> {

  explicit Entity_CX_StorageClass(EntityScope parent_h)
      : Bind_CX_StorageClass<std::decay_t<pybind11::enum_<CX_StorageClass>>>(
            parent_h),
        handle{parent_h, "CX_StorageClass", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Represents the storage classes as declared in the source. CX_SC_Invalid
 * was added for the case that the passed cursor in not a declaration.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CX_StorageClass> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CX_StorageClass

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getStorageClass
template <class Pybind11T>
struct Bind_clang_Cursor_getStorageClass : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getStorageClass(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getStorageClass",
                 static_cast<CX_StorageClass (*)(CXCursor)>(
                     &clang_Cursor_getStorageClass),
                 R"_pb11_weaver(/**
 * Returns the storage class for a function or variable declaration.
 *
 * If the passed in Cursor is not a function or variable declaration,
 * CX_SC_Invalid is returned else the storage class.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getStorageClass"; }
};

struct Entity_clang_Cursor_getStorageClass
    : public Bind_clang_Cursor_getStorageClass<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getStorageClass(EntityScope parent_h)
      : Bind_clang_Cursor_getStorageClass<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getStorageClass

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNumOverloadedDecls
template <class Pybind11T>
struct Bind_clang_getNumOverloadedDecls : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNumOverloadedDecls(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getNumOverloadedDecls",
        static_cast<unsigned int (*)(CXCursor)>(&clang_getNumOverloadedDecls),
        R"_pb11_weaver(/**
 * Determine the number of overloaded declarations referenced by a
 * \c CXCursor_OverloadedDeclRef cursor.
 *
 * \param cursor The cursor whose overloaded declarations are being queried.
 *
 * \returns The number of overloaded declarations referenced by \c cursor. If it
 * is not a \c CXCursor_OverloadedDeclRef cursor, returns 0.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNumOverloadedDecls"; }
};

struct Entity_clang_getNumOverloadedDecls
    : public Bind_clang_getNumOverloadedDecls<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNumOverloadedDecls(EntityScope parent_h)
      : Bind_clang_getNumOverloadedDecls<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNumOverloadedDecls

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getOverloadedDecl
template <class Pybind11T>
struct Bind_clang_getOverloadedDecl : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getOverloadedDecl(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getOverloadedDecl",
                 static_cast<CXCursor (*)(CXCursor, unsigned int)>(
                     &clang_getOverloadedDecl),
                 R"_pb11_weaver(/**
 * Retrieve a cursor for one of the overloaded declarations referenced
 * by a \c CXCursor_OverloadedDeclRef cursor.
 *
 * \param cursor The cursor whose overloaded declarations are being queried.
 *
 * \param index The zero-based index into the set of overloaded declarations in
 * the cursor.
 *
 * \returns A cursor representing the declaration referenced by the given
 * \c cursor at the specified \c index. If the cursor does not have an
 * associated set of overloaded declarations, or if the index is out of bounds,
 * returns \c clang_getNullCursor();
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getOverloadedDecl"; }
};

struct Entity_clang_getOverloadedDecl
    : public Bind_clang_getOverloadedDecl<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getOverloadedDecl(EntityScope parent_h)
      : Bind_clang_getOverloadedDecl<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getOverloadedDecl

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getIBOutletCollectionType
template <class Pybind11T>
struct Bind_clang_getIBOutletCollectionType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getIBOutletCollectionType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getIBOutletCollectionType",
        static_cast<CXType (*)(CXCursor)>(&clang_getIBOutletCollectionType),
        R"_pb11_weaver(/**
 * For cursors representing an iboutletcollection attribute,
 *  this function returns the collection element type.
 *
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getIBOutletCollectionType"; }
};

struct Entity_clang_getIBOutletCollectionType
    : public Bind_clang_getIBOutletCollectionType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getIBOutletCollectionType(EntityScope parent_h)
      : Bind_clang_getIBOutletCollectionType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getIBOutletCollectionType

#ifndef PB11_WEAVER_DISABLE_Entity_CXChildVisitResult
template <class Pybind11T> struct Bind_CXChildVisitResult : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXChildVisitResult(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXChildVisit_Break", CXChildVisitResult::CXChildVisit_Break,
                   R"_pb11_weaver(/**
   * Terminates the cursor traversal.
   */)_pb11_weaver");
    pb11_obj.value("CXChildVisit_Continue",
                   CXChildVisitResult::CXChildVisit_Continue, R"_pb11_weaver(/**
   * Continues the cursor traversal with the next sibling of
   * the cursor just visited, without visiting its children.
   */)_pb11_weaver");
    pb11_obj.value("CXChildVisit_Recurse",
                   CXChildVisitResult::CXChildVisit_Recurse, R"_pb11_weaver(/**
   * Recursively traverse the children of this cursor, using
   * the same visitor and client data.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXChildVisitResult"; }
};

struct Entity_CXChildVisitResult
    : public Bind_CXChildVisitResult<
          std::decay_t<pybind11::enum_<CXChildVisitResult>>> {

  explicit Entity_CXChildVisitResult(EntityScope parent_h)
      : Bind_CXChildVisitResult<
            std::decay_t<pybind11::enum_<CXChildVisitResult>>>(parent_h),
        handle{parent_h, "CXChildVisitResult", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes how the traversal of the children of a particular
 * cursor should proceed after visiting a particular child cursor.
 *
 * A value of this enumeration type should be returned by each
 * \c CXCursorVisitor to indicate how clang_visitChildren() proceed.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXChildVisitResult> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXChildVisitResult

#ifndef PB11_WEAVER_DISABLE_Entity_clang_visitChildren
template <class Pybind11T> struct Bind_clang_visitChildren : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_visitChildren(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_visitChildren",
        [=](CXCursor parent,
            std::function<CXChildVisitResult(
                CXCursor, CXCursor, pybind11_weaver::PointerWrapper<void *> *)>
                visitor,
            pybind11_weaver::PointerWrapper<void *> *client_data) {
          return clang_visitChildren(
              parent,
              pybind11_weaver::FnPointerWrapper<
                  CXChildVisitResult, CXCursor, CXCursor,
                  pybind11_weaver::PointerWrapper<void *> *>::
                  GetCptr<CXChildVisitResult, CXCursor, CXCursor, void *>::Run(
                      (visitor), pybind11_weaver::Guardian(),
                      [](CXCursor arg0_0, CXCursor arg0_1, void *arg0_2) {
                        auto &to_call = pybind11_weaver::FnPointerWrapper<
                            CXChildVisitResult, CXCursor, CXCursor,
                            pybind11_weaver::PointerWrapper<void *>
                                *>::FnProxy(1);
                        return to_call(
                            arg0_0, arg0_1,
                            pybind11_weaver::WrapP<void *>(arg0_2).get());
                      },
                      1),
              (client_data)->ptr);
        },
        R"_pb11_weaver(/**
 * Visit the children of a particular cursor.
 *
 * This function visits all the direct children of the given cursor,
 * invoking the given \p visitor function with the cursors of each
 * visited child. The traversal may be recursive, if the visitor returns
 * \c CXChildVisit_Recurse. The traversal may also be ended prematurely, if
 * the visitor returns \c CXChildVisit_Break.
 *
 * \param parent the cursor whose child may be visited. All kinds of
 * cursors can be visited, including invalid cursors (which, by
 * definition, have no children).
 *
 * \param visitor the visitor function that will be invoked for each
 * child of \p parent.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the visitor each time it is invoked.
 *
 * \returns a non-zero value if the traversal was terminated
 * prematurely by the visitor returning \c CXChildVisit_Break.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_visitChildren"; }
};

struct Entity_clang_visitChildren
    : public Bind_clang_visitChildren<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_visitChildren(EntityScope parent_h)
      : Bind_clang_visitChildren<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_visitChildren

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorUSR
template <class Pybind11T> struct Bind_clang_getCursorUSR : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorUSR(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCursorUSR",
                 static_cast<CXString (*)(CXCursor)>(&clang_getCursorUSR),
                 R"_pb11_weaver(/**
 * Retrieve a Unified Symbol Resolution (USR) for the entity referenced
 * by the given cursor.
 *
 * A Unified Symbol Resolution (USR) is a string that identifies a particular
 * entity (function, class, variable, etc.) within a program. USRs can be
 * compared across translation units to determine, e.g., when references in
 * one translation refer to an entity defined in another translation unit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorUSR"; }
};

struct Entity_clang_getCursorUSR
    : public Bind_clang_getCursorUSR<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorUSR(EntityScope parent_h)
      : Bind_clang_getCursorUSR<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorUSR

#ifndef PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCClass
template <class Pybind11T>
struct Bind_clang_constructUSR_ObjCClass : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_constructUSR_ObjCClass(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_constructUSR_ObjCClass",
        static_cast<CXString (*)(const char *)>(&clang_constructUSR_ObjCClass),
        R"_pb11_weaver(/**
 * Construct a USR for a specified Objective-C class.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_constructUSR_ObjCClass"; }
};

struct Entity_clang_constructUSR_ObjCClass
    : public Bind_clang_constructUSR_ObjCClass<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_constructUSR_ObjCClass(EntityScope parent_h)
      : Bind_clang_constructUSR_ObjCClass<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCClass

#ifndef PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCCategory
template <class Pybind11T>
struct Bind_clang_constructUSR_ObjCCategory : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_constructUSR_ObjCCategory(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_constructUSR_ObjCCategory",
                 static_cast<CXString (*)(const char *, const char *)>(
                     &clang_constructUSR_ObjCCategory),
                 R"_pb11_weaver(/**
 * Construct a USR for a specified Objective-C category.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_constructUSR_ObjCCategory"; }
};

struct Entity_clang_constructUSR_ObjCCategory
    : public Bind_clang_constructUSR_ObjCCategory<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_constructUSR_ObjCCategory(EntityScope parent_h)
      : Bind_clang_constructUSR_ObjCCategory<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCCategory

#ifndef PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCProtocol
template <class Pybind11T>
struct Bind_clang_constructUSR_ObjCProtocol : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_constructUSR_ObjCProtocol(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_constructUSR_ObjCProtocol",
                 static_cast<CXString (*)(const char *)>(
                     &clang_constructUSR_ObjCProtocol),
                 R"_pb11_weaver(/**
 * Construct a USR for a specified Objective-C protocol.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_constructUSR_ObjCProtocol"; }
};

struct Entity_clang_constructUSR_ObjCProtocol
    : public Bind_clang_constructUSR_ObjCProtocol<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_constructUSR_ObjCProtocol(EntityScope parent_h)
      : Bind_clang_constructUSR_ObjCProtocol<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCProtocol

#ifndef PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCIvar
template <class Pybind11T>
struct Bind_clang_constructUSR_ObjCIvar : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_constructUSR_ObjCIvar(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_constructUSR_ObjCIvar",
                 static_cast<CXString (*)(const char *, CXString)>(
                     &clang_constructUSR_ObjCIvar),
                 R"_pb11_weaver(/**
 * Construct a USR for a specified Objective-C instance variable and
 *   the USR for its containing class.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_constructUSR_ObjCIvar"; }
};

struct Entity_clang_constructUSR_ObjCIvar
    : public Bind_clang_constructUSR_ObjCIvar<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_constructUSR_ObjCIvar(EntityScope parent_h)
      : Bind_clang_constructUSR_ObjCIvar<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCIvar

#ifndef PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCMethod
template <class Pybind11T>
struct Bind_clang_constructUSR_ObjCMethod : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_constructUSR_ObjCMethod(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_constructUSR_ObjCMethod",
        static_cast<CXString (*)(const char *, unsigned int, CXString)>(
            &clang_constructUSR_ObjCMethod),
        R"_pb11_weaver(/**
 * Construct a USR for a specified Objective-C method and
 *   the USR for its containing class.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_constructUSR_ObjCMethod"; }
};

struct Entity_clang_constructUSR_ObjCMethod
    : public Bind_clang_constructUSR_ObjCMethod<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_constructUSR_ObjCMethod(EntityScope parent_h)
      : Bind_clang_constructUSR_ObjCMethod<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCMethod

#ifndef PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCProperty
template <class Pybind11T>
struct Bind_clang_constructUSR_ObjCProperty : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_constructUSR_ObjCProperty(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_constructUSR_ObjCProperty",
                 static_cast<CXString (*)(const char *, CXString)>(
                     &clang_constructUSR_ObjCProperty),
                 R"_pb11_weaver(/**
 * Construct a USR for a specified Objective-C property and the USR
 *  for its containing class.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_constructUSR_ObjCProperty"; }
};

struct Entity_clang_constructUSR_ObjCProperty
    : public Bind_clang_constructUSR_ObjCProperty<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_constructUSR_ObjCProperty(EntityScope parent_h)
      : Bind_clang_constructUSR_ObjCProperty<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_constructUSR_ObjCProperty

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorSpelling
template <class Pybind11T>
struct Bind_clang_getCursorSpelling : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorSpelling(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCursorSpelling",
                 static_cast<CXString (*)(CXCursor)>(&clang_getCursorSpelling),
                 R"_pb11_weaver(/**
 * Retrieve a name for the entity referenced by this cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorSpelling"; }
};

struct Entity_clang_getCursorSpelling
    : public Bind_clang_getCursorSpelling<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorSpelling(EntityScope parent_h)
      : Bind_clang_getCursorSpelling<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorSpelling

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getSpellingNameRange
template <class Pybind11T>
struct Bind_clang_Cursor_getSpellingNameRange : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getSpellingNameRange(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getSpellingNameRange",
        static_cast<CXSourceRange (*)(CXCursor, unsigned int, unsigned int)>(
            &clang_Cursor_getSpellingNameRange),
        R"_pb11_weaver(/**
 * Retrieve a range for a piece that forms the cursors spelling name.
 * Most of the times there is only one range for the complete spelling but for
 * Objective-C methods and Objective-C message expressions, there are multiple
 * pieces for each selector identifier.
 *
 * \param pieceIndex the index of the spelling name piece. If this is greater
 * than the actual number of pieces, it will return a NULL (invalid) range.
 *
 * \param options Reserved.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getSpellingNameRange"; }
};

struct Entity_clang_Cursor_getSpellingNameRange
    : public Bind_clang_Cursor_getSpellingNameRange<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getSpellingNameRange(EntityScope parent_h)
      : Bind_clang_Cursor_getSpellingNameRange<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getSpellingNameRange

#ifndef PB11_WEAVER_DISABLE_Entity_CXPrintingPolicyProperty
template <class Pybind11T>
struct Bind_CXPrintingPolicyProperty : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXPrintingPolicyProperty(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXPrintingPolicy_Indentation",
                   CXPrintingPolicyProperty::CXPrintingPolicy_Indentation);
    pb11_obj.value(
        "CXPrintingPolicy_SuppressSpecifiers",
        CXPrintingPolicyProperty::CXPrintingPolicy_SuppressSpecifiers);
    pb11_obj.value(
        "CXPrintingPolicy_SuppressTagKeyword",
        CXPrintingPolicyProperty::CXPrintingPolicy_SuppressTagKeyword);
    pb11_obj.value(
        "CXPrintingPolicy_IncludeTagDefinition",
        CXPrintingPolicyProperty::CXPrintingPolicy_IncludeTagDefinition);
    pb11_obj.value("CXPrintingPolicy_SuppressScope",
                   CXPrintingPolicyProperty::CXPrintingPolicy_SuppressScope);
    pb11_obj.value(
        "CXPrintingPolicy_SuppressUnwrittenScope",
        CXPrintingPolicyProperty::CXPrintingPolicy_SuppressUnwrittenScope);
    pb11_obj.value(
        "CXPrintingPolicy_SuppressInitializers",
        CXPrintingPolicyProperty::CXPrintingPolicy_SuppressInitializers);
    pb11_obj.value(
        "CXPrintingPolicy_ConstantArraySizeAsWritten",
        CXPrintingPolicyProperty::CXPrintingPolicy_ConstantArraySizeAsWritten);
    pb11_obj.value(
        "CXPrintingPolicy_AnonymousTagLocations",
        CXPrintingPolicyProperty::CXPrintingPolicy_AnonymousTagLocations);
    pb11_obj.value(
        "CXPrintingPolicy_SuppressStrongLifetime",
        CXPrintingPolicyProperty::CXPrintingPolicy_SuppressStrongLifetime);
    pb11_obj.value(
        "CXPrintingPolicy_SuppressLifetimeQualifiers",
        CXPrintingPolicyProperty::CXPrintingPolicy_SuppressLifetimeQualifiers);
    pb11_obj.value("CXPrintingPolicy_SuppressTemplateArgsInCXXConstructors",
                   CXPrintingPolicyProperty::
                       CXPrintingPolicy_SuppressTemplateArgsInCXXConstructors);
    pb11_obj.value("CXPrintingPolicy_Bool",
                   CXPrintingPolicyProperty::CXPrintingPolicy_Bool);
    pb11_obj.value("CXPrintingPolicy_Restrict",
                   CXPrintingPolicyProperty::CXPrintingPolicy_Restrict);
    pb11_obj.value("CXPrintingPolicy_Alignof",
                   CXPrintingPolicyProperty::CXPrintingPolicy_Alignof);
    pb11_obj.value(
        "CXPrintingPolicy_UnderscoreAlignof",
        CXPrintingPolicyProperty::CXPrintingPolicy_UnderscoreAlignof);
    pb11_obj.value(
        "CXPrintingPolicy_UseVoidForZeroParams",
        CXPrintingPolicyProperty::CXPrintingPolicy_UseVoidForZeroParams);
    pb11_obj.value("CXPrintingPolicy_TerseOutput",
                   CXPrintingPolicyProperty::CXPrintingPolicy_TerseOutput);
    pb11_obj.value(
        "CXPrintingPolicy_PolishForDeclaration",
        CXPrintingPolicyProperty::CXPrintingPolicy_PolishForDeclaration);
    pb11_obj.value("CXPrintingPolicy_Half",
                   CXPrintingPolicyProperty::CXPrintingPolicy_Half);
    pb11_obj.value("CXPrintingPolicy_MSWChar",
                   CXPrintingPolicyProperty::CXPrintingPolicy_MSWChar);
    pb11_obj.value("CXPrintingPolicy_IncludeNewlines",
                   CXPrintingPolicyProperty::CXPrintingPolicy_IncludeNewlines);
    pb11_obj.value("CXPrintingPolicy_MSVCFormatting",
                   CXPrintingPolicyProperty::CXPrintingPolicy_MSVCFormatting);
    pb11_obj.value(
        "CXPrintingPolicy_ConstantsAsWritten",
        CXPrintingPolicyProperty::CXPrintingPolicy_ConstantsAsWritten);
    pb11_obj.value(
        "CXPrintingPolicy_SuppressImplicitBase",
        CXPrintingPolicyProperty::CXPrintingPolicy_SuppressImplicitBase);
    pb11_obj.value(
        "CXPrintingPolicy_FullyQualifiedName",
        CXPrintingPolicyProperty::CXPrintingPolicy_FullyQualifiedName);
    pb11_obj.value("CXPrintingPolicy_LastProperty",
                   CXPrintingPolicyProperty::CXPrintingPolicy_LastProperty);
  }

  static const char *Key() { return "CXPrintingPolicyProperty"; }
};

struct Entity_CXPrintingPolicyProperty
    : public Bind_CXPrintingPolicyProperty<
          std::decay_t<pybind11::enum_<CXPrintingPolicyProperty>>> {

  explicit Entity_CXPrintingPolicyProperty(EntityScope parent_h)
      : Bind_CXPrintingPolicyProperty<
            std::decay_t<pybind11::enum_<CXPrintingPolicyProperty>>>(parent_h),
        handle{parent_h, "CXPrintingPolicyProperty", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Properties for the printing policy.
 *
 * See \c clang::PrintingPolicy for more information.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXPrintingPolicyProperty> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXPrintingPolicyProperty

#ifndef PB11_WEAVER_DISABLE_Entity_clang_PrintingPolicy_getProperty
template <class Pybind11T>
struct Bind_clang_PrintingPolicy_getProperty : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_PrintingPolicy_getProperty(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_PrintingPolicy_getProperty",
        [=](pybind11_weaver::PointerWrapper<void *> *Policy,
            CXPrintingPolicyProperty Property) {
          return clang_PrintingPolicy_getProperty((Policy)->ptr, Property);
        },
        R"_pb11_weaver(/**
 * Get a property value for the given printing policy.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_PrintingPolicy_getProperty"; }
};

struct Entity_clang_PrintingPolicy_getProperty
    : public Bind_clang_PrintingPolicy_getProperty<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_PrintingPolicy_getProperty(EntityScope parent_h)
      : Bind_clang_PrintingPolicy_getProperty<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_PrintingPolicy_getProperty

#ifndef PB11_WEAVER_DISABLE_Entity_clang_PrintingPolicy_setProperty
template <class Pybind11T>
struct Bind_clang_PrintingPolicy_setProperty : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_PrintingPolicy_setProperty(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_PrintingPolicy_setProperty",
        [=](pybind11_weaver::PointerWrapper<void *> *Policy,
            CXPrintingPolicyProperty Property, unsigned int Value) {
          return clang_PrintingPolicy_setProperty((Policy)->ptr, Property,
                                                  Value);
        },
        R"_pb11_weaver(/**
 * Set a property value for the given printing policy.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_PrintingPolicy_setProperty"; }
};

struct Entity_clang_PrintingPolicy_setProperty
    : public Bind_clang_PrintingPolicy_setProperty<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_PrintingPolicy_setProperty(EntityScope parent_h)
      : Bind_clang_PrintingPolicy_setProperty<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_PrintingPolicy_setProperty

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorPrintingPolicy
template <class Pybind11T>
struct Bind_clang_getCursorPrintingPolicy : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorPrintingPolicy(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorPrintingPolicy",
        [=](CXCursor arg0) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getCursorPrintingPolicy(arg0))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve the default policy for the cursor.
 *
 * The policy should be released after use with \c
 * clang_PrintingPolicy_dispose.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorPrintingPolicy"; }
};

struct Entity_clang_getCursorPrintingPolicy
    : public Bind_clang_getCursorPrintingPolicy<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorPrintingPolicy(EntityScope parent_h)
      : Bind_clang_getCursorPrintingPolicy<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorPrintingPolicy

#ifndef PB11_WEAVER_DISABLE_Entity_clang_PrintingPolicy_dispose
template <class Pybind11T>
struct Bind_clang_PrintingPolicy_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_PrintingPolicy_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_PrintingPolicy_dispose",
        [=](pybind11_weaver::PointerWrapper<void *> *Policy) {
          return clang_PrintingPolicy_dispose((Policy)->ptr);
        },
        R"_pb11_weaver(/**
 * Release a printing policy.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_PrintingPolicy_dispose"; }
};

struct Entity_clang_PrintingPolicy_dispose
    : public Bind_clang_PrintingPolicy_dispose<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_PrintingPolicy_dispose(EntityScope parent_h)
      : Bind_clang_PrintingPolicy_dispose<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_PrintingPolicy_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorPrettyPrinted
template <class Pybind11T>
struct Bind_clang_getCursorPrettyPrinted : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorPrettyPrinted(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorPrettyPrinted",
        [=](CXCursor Cursor, pybind11_weaver::PointerWrapper<void *> *Policy) {
          return clang_getCursorPrettyPrinted(Cursor, (Policy)->ptr);
        },
        R"_pb11_weaver(/**
 * Pretty print declarations.
 *
 * \param Cursor The cursor representing a declaration.
 *
 * \param Policy The policy to control the entities being printed. If
 * NULL, a default policy is used.
 *
 * \returns The pretty printed declaration or the empty string for
 * other cursors.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorPrettyPrinted"; }
};

struct Entity_clang_getCursorPrettyPrinted
    : public Bind_clang_getCursorPrettyPrinted<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorPrettyPrinted(EntityScope parent_h)
      : Bind_clang_getCursorPrettyPrinted<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorPrettyPrinted

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorDisplayName
template <class Pybind11T>
struct Bind_clang_getCursorDisplayName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorDisplayName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorDisplayName",
        static_cast<CXString (*)(CXCursor)>(&clang_getCursorDisplayName),
        R"_pb11_weaver(/**
 * Retrieve the display name for the entity referenced by this cursor.
 *
 * The display name contains extra information that helps identify the cursor,
 * such as the parameters of a function or template or the arguments of a
 * class template specialization.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorDisplayName"; }
};

struct Entity_clang_getCursorDisplayName
    : public Bind_clang_getCursorDisplayName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorDisplayName(EntityScope parent_h)
      : Bind_clang_getCursorDisplayName<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorDisplayName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorReferenced
template <class Pybind11T>
struct Bind_clang_getCursorReferenced : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorReferenced(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorReferenced",
        static_cast<CXCursor (*)(CXCursor)>(&clang_getCursorReferenced),
        R"_pb11_weaver(/** For a cursor that is a reference, retrieve a cursor representing the
 * entity that it references.
 *
 * Reference cursors refer to other entities in the AST. For example, an
 * Objective-C superclass reference cursor refers to an Objective-C class.
 * This function produces the cursor for the Objective-C class from the
 * cursor for the superclass reference. If the input cursor is a declaration or
 * definition, it returns that declaration or definition unchanged.
 * Otherwise, returns the NULL cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorReferenced"; }
};

struct Entity_clang_getCursorReferenced
    : public Bind_clang_getCursorReferenced<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorReferenced(EntityScope parent_h)
      : Bind_clang_getCursorReferenced<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorReferenced

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorDefinition
template <class Pybind11T>
struct Bind_clang_getCursorDefinition : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorDefinition(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorDefinition",
        static_cast<CXCursor (*)(CXCursor)>(&clang_getCursorDefinition),
        R"_pb11_weaver(/**
 *  For a cursor that is either a reference to or a declaration
 *  of some entity, retrieve a cursor that describes the definition of
 *  that entity.
 *
 *  Some entities can be declared multiple times within a translation
 *  unit, but only one of those declarations can also be a
 *  definition. For example, given:
 *
 *  \code
 *  int f(int, int);
 *  int g(int x, int y) { return f(x, y); }
 *  int f(int a, int b) { return a + b; }
 *  int f(int, int);
 *  \endcode
 *
 *  there are three declarations of the function "f", but only the
 *  second one is a definition. The clang_getCursorDefinition()
 *  function will take any cursor pointing to a declaration of "f"
 *  (the first or fourth lines of the example) or a cursor referenced
 *  that uses "f" (the call to "f' inside "g") and will return a
 *  declaration cursor pointing to the definition (the second "f"
 *  declaration).
 *
 *  If given a cursor for which there is no corresponding definition,
 *  e.g., because there is no definition of that entity within this
 *  translation unit, returns a NULL cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorDefinition"; }
};

struct Entity_clang_getCursorDefinition
    : public Bind_clang_getCursorDefinition<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorDefinition(EntityScope parent_h)
      : Bind_clang_getCursorDefinition<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorDefinition

#ifndef PB11_WEAVER_DISABLE_Entity_clang_isCursorDefinition
template <class Pybind11T>
struct Bind_clang_isCursorDefinition : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_isCursorDefinition(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_isCursorDefinition",
        static_cast<unsigned int (*)(CXCursor)>(&clang_isCursorDefinition),
        R"_pb11_weaver(/**
 * Determine whether the declaration pointed to by this cursor
 * is also a definition of that entity.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_isCursorDefinition"; }
};

struct Entity_clang_isCursorDefinition
    : public Bind_clang_isCursorDefinition<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_isCursorDefinition(EntityScope parent_h)
      : Bind_clang_isCursorDefinition<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_isCursorDefinition

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCanonicalCursor
template <class Pybind11T>
struct Bind_clang_getCanonicalCursor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCanonicalCursor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCanonicalCursor",
                 static_cast<CXCursor (*)(CXCursor)>(&clang_getCanonicalCursor),
                 R"_pb11_weaver(/**
 * Retrieve the canonical cursor corresponding to the given cursor.
 *
 * In the C family of languages, many kinds of entities can be declared several
 * times within a single translation unit. For example, a structure type can
 * be forward-declared (possibly multiple times) and later defined:
 *
 * \code
 * struct X;
 * struct X;
 * struct X {
 *   int member;
 * };
 * \endcode
 *
 * The declarations and the definition of \c X are represented by three
 * different cursors, all of which are declarations of the same underlying
 * entity. One of these cursor is considered the "canonical" cursor, which
 * is effectively the representative for the underlying entity. One can
 * determine if two cursors are declarations of the same underlying entity by
 * comparing their canonical cursors.
 *
 * \returns The canonical cursor for the entity referred to by the given cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCanonicalCursor"; }
};

struct Entity_clang_getCanonicalCursor
    : public Bind_clang_getCanonicalCursor<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCanonicalCursor(EntityScope parent_h)
      : Bind_clang_getCanonicalCursor<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCanonicalCursor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCSelectorIndex
template <class Pybind11T>
struct Bind_clang_Cursor_getObjCSelectorIndex : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getObjCSelectorIndex(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getObjCSelectorIndex",
        static_cast<int (*)(CXCursor)>(&clang_Cursor_getObjCSelectorIndex),
        R"_pb11_weaver(/**
 * If the cursor points to a selector identifier in an Objective-C
 * method or message expression, this returns the selector index.
 *
 * After getting a cursor with #clang_getCursor, this can be called to
 * determine if the location points to a selector identifier.
 *
 * \returns The selector index if the cursor is an Objective-C method or message
 * expression and the cursor is pointing to a selector identifier, or -1
 * otherwise.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getObjCSelectorIndex"; }
};

struct Entity_clang_Cursor_getObjCSelectorIndex
    : public Bind_clang_Cursor_getObjCSelectorIndex<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getObjCSelectorIndex(EntityScope parent_h)
      : Bind_clang_Cursor_getObjCSelectorIndex<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCSelectorIndex

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isDynamicCall
template <class Pybind11T>
struct Bind_clang_Cursor_isDynamicCall : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isDynamicCall(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_isDynamicCall",
                 static_cast<int (*)(CXCursor)>(&clang_Cursor_isDynamicCall),
                 R"_pb11_weaver(/**
 * Given a cursor pointing to a C++ method call or an Objective-C
 * message, returns non-zero if the method/message is "dynamic", meaning:
 *
 * For a C++ method: the call is virtual.
 * For an Objective-C message: the receiver is an object instance, not 'super'
 * or a specific class.
 *
 * If the method/message is "static" or the cursor does not point to a
 * method/message, it will return zero.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isDynamicCall"; }
};

struct Entity_clang_Cursor_isDynamicCall
    : public Bind_clang_Cursor_isDynamicCall<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isDynamicCall(EntityScope parent_h)
      : Bind_clang_Cursor_isDynamicCall<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isDynamicCall

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getReceiverType
template <class Pybind11T>
struct Bind_clang_Cursor_getReceiverType : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getReceiverType(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getReceiverType",
        static_cast<CXType (*)(CXCursor)>(&clang_Cursor_getReceiverType),
        R"_pb11_weaver(/**
 * Given a cursor pointing to an Objective-C message or property
 * reference, or C++ method call, returns the CXType of the receiver.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getReceiverType"; }
};

struct Entity_clang_Cursor_getReceiverType
    : public Bind_clang_Cursor_getReceiverType<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getReceiverType(EntityScope parent_h)
      : Bind_clang_Cursor_getReceiverType<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getReceiverType

#ifndef PB11_WEAVER_DISABLE_Entity_CXObjCPropertyAttrKind
template <class Pybind11T>
struct Bind_CXObjCPropertyAttrKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXObjCPropertyAttrKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXObjCPropertyAttr_noattr",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_noattr);
    pb11_obj.value("CXObjCPropertyAttr_readonly",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_readonly);
    pb11_obj.value("CXObjCPropertyAttr_getter",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_getter);
    pb11_obj.value("CXObjCPropertyAttr_assign",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_assign);
    pb11_obj.value("CXObjCPropertyAttr_readwrite",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_readwrite);
    pb11_obj.value("CXObjCPropertyAttr_retain",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_retain);
    pb11_obj.value("CXObjCPropertyAttr_copy",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_copy);
    pb11_obj.value("CXObjCPropertyAttr_nonatomic",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_nonatomic);
    pb11_obj.value("CXObjCPropertyAttr_setter",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_setter);
    pb11_obj.value("CXObjCPropertyAttr_atomic",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_atomic);
    pb11_obj.value("CXObjCPropertyAttr_weak",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_weak);
    pb11_obj.value("CXObjCPropertyAttr_strong",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_strong);
    pb11_obj.value(
        "CXObjCPropertyAttr_unsafe_unretained",
        CXObjCPropertyAttrKind::CXObjCPropertyAttr_unsafe_unretained);
    pb11_obj.value("CXObjCPropertyAttr_class",
                   CXObjCPropertyAttrKind::CXObjCPropertyAttr_class);
  }

  static const char *Key() { return "CXObjCPropertyAttrKind"; }
};

struct Entity_CXObjCPropertyAttrKind
    : public Bind_CXObjCPropertyAttrKind<
          std::decay_t<pybind11::enum_<CXObjCPropertyAttrKind>>> {

  explicit Entity_CXObjCPropertyAttrKind(EntityScope parent_h)
      : Bind_CXObjCPropertyAttrKind<
            std::decay_t<pybind11::enum_<CXObjCPropertyAttrKind>>>(parent_h),
        handle{parent_h, "CXObjCPropertyAttrKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Property attributes for a \c CXCursor_ObjCPropertyDecl.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXObjCPropertyAttrKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXObjCPropertyAttrKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCPropertyAttributes
template <class Pybind11T>
struct Bind_clang_Cursor_getObjCPropertyAttributes : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getObjCPropertyAttributes(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getObjCPropertyAttributes",
                 static_cast<unsigned int (*)(CXCursor, unsigned int)>(
                     &clang_Cursor_getObjCPropertyAttributes),
                 R"_pb11_weaver(/**
 * Given a cursor that represents a property declaration, return the
 * associated property attributes. The bits are formed from
 * \c CXObjCPropertyAttrKind.
 *
 * \param reserved Reserved for future use, pass 0.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getObjCPropertyAttributes"; }
};

struct Entity_clang_Cursor_getObjCPropertyAttributes
    : public Bind_clang_Cursor_getObjCPropertyAttributes<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getObjCPropertyAttributes(EntityScope parent_h)
      : Bind_clang_Cursor_getObjCPropertyAttributes<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCPropertyAttributes

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCPropertyGetterName
template <class Pybind11T>
struct Bind_clang_Cursor_getObjCPropertyGetterName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getObjCPropertyGetterName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getObjCPropertyGetterName",
                 static_cast<CXString (*)(CXCursor)>(
                     &clang_Cursor_getObjCPropertyGetterName),
                 R"_pb11_weaver(/**
 * Given a cursor that represents a property declaration, return the
 * name of the method that implements the getter.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getObjCPropertyGetterName"; }
};

struct Entity_clang_Cursor_getObjCPropertyGetterName
    : public Bind_clang_Cursor_getObjCPropertyGetterName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getObjCPropertyGetterName(EntityScope parent_h)
      : Bind_clang_Cursor_getObjCPropertyGetterName<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCPropertyGetterName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCPropertySetterName
template <class Pybind11T>
struct Bind_clang_Cursor_getObjCPropertySetterName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getObjCPropertySetterName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getObjCPropertySetterName",
                 static_cast<CXString (*)(CXCursor)>(
                     &clang_Cursor_getObjCPropertySetterName),
                 R"_pb11_weaver(/**
 * Given a cursor that represents a property declaration, return the
 * name of the method that implements the setter, if any.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getObjCPropertySetterName"; }
};

struct Entity_clang_Cursor_getObjCPropertySetterName
    : public Bind_clang_Cursor_getObjCPropertySetterName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getObjCPropertySetterName(EntityScope parent_h)
      : Bind_clang_Cursor_getObjCPropertySetterName<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCPropertySetterName

#ifndef PB11_WEAVER_DISABLE_Entity_CXObjCDeclQualifierKind
template <class Pybind11T>
struct Bind_CXObjCDeclQualifierKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXObjCDeclQualifierKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXObjCDeclQualifier_None",
                   CXObjCDeclQualifierKind::CXObjCDeclQualifier_None);
    pb11_obj.value("CXObjCDeclQualifier_In",
                   CXObjCDeclQualifierKind::CXObjCDeclQualifier_In);
    pb11_obj.value("CXObjCDeclQualifier_Inout",
                   CXObjCDeclQualifierKind::CXObjCDeclQualifier_Inout);
    pb11_obj.value("CXObjCDeclQualifier_Out",
                   CXObjCDeclQualifierKind::CXObjCDeclQualifier_Out);
    pb11_obj.value("CXObjCDeclQualifier_Bycopy",
                   CXObjCDeclQualifierKind::CXObjCDeclQualifier_Bycopy);
    pb11_obj.value("CXObjCDeclQualifier_Byref",
                   CXObjCDeclQualifierKind::CXObjCDeclQualifier_Byref);
    pb11_obj.value("CXObjCDeclQualifier_Oneway",
                   CXObjCDeclQualifierKind::CXObjCDeclQualifier_Oneway);
  }

  static const char *Key() { return "CXObjCDeclQualifierKind"; }
};

struct Entity_CXObjCDeclQualifierKind
    : public Bind_CXObjCDeclQualifierKind<
          std::decay_t<pybind11::enum_<CXObjCDeclQualifierKind>>> {

  explicit Entity_CXObjCDeclQualifierKind(EntityScope parent_h)
      : Bind_CXObjCDeclQualifierKind<
            std::decay_t<pybind11::enum_<CXObjCDeclQualifierKind>>>(parent_h),
        handle{parent_h, "CXObjCDeclQualifierKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * 'Qualifiers' written next to the return and parameter types in
 * Objective-C method declarations.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXObjCDeclQualifierKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXObjCDeclQualifierKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCDeclQualifiers
template <class Pybind11T>
struct Bind_clang_Cursor_getObjCDeclQualifiers : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getObjCDeclQualifiers(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getObjCDeclQualifiers",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_Cursor_getObjCDeclQualifiers),
                 R"_pb11_weaver(/**
 * Given a cursor that represents an Objective-C method or parameter
 * declaration, return the associated Objective-C qualifiers for the return
 * type or the parameter respectively. The bits are formed from
 * CXObjCDeclQualifierKind.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getObjCDeclQualifiers"; }
};

struct Entity_clang_Cursor_getObjCDeclQualifiers
    : public Bind_clang_Cursor_getObjCDeclQualifiers<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getObjCDeclQualifiers(EntityScope parent_h)
      : Bind_clang_Cursor_getObjCDeclQualifiers<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCDeclQualifiers

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isObjCOptional
template <class Pybind11T>
struct Bind_clang_Cursor_isObjCOptional : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isObjCOptional(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_isObjCOptional",
        static_cast<unsigned int (*)(CXCursor)>(&clang_Cursor_isObjCOptional),
        R"_pb11_weaver(/**
 * Given a cursor that represents an Objective-C method or property
 * declaration, return non-zero if the declaration was affected by "\@optional".
 * Returns zero if the cursor is not such a declaration or it is "\@required".
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isObjCOptional"; }
};

struct Entity_clang_Cursor_isObjCOptional
    : public Bind_clang_Cursor_isObjCOptional<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isObjCOptional(EntityScope parent_h)
      : Bind_clang_Cursor_isObjCOptional<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isObjCOptional

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isVariadic
template <class Pybind11T>
struct Bind_clang_Cursor_isVariadic : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isVariadic(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_isVariadic",
        static_cast<unsigned int (*)(CXCursor)>(&clang_Cursor_isVariadic),
        R"_pb11_weaver(/**
 * Returns non-zero if the given cursor is a variadic function or method.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isVariadic"; }
};

struct Entity_clang_Cursor_isVariadic
    : public Bind_clang_Cursor_isVariadic<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isVariadic(EntityScope parent_h)
      : Bind_clang_Cursor_isVariadic<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isVariadic

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_isExternalSymbol
template <class Pybind11T>
struct Bind_clang_Cursor_isExternalSymbol : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_isExternalSymbol(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_isExternalSymbol",
                 static_cast<unsigned int (*)(CXCursor, CXString *, CXString *,
                                              unsigned int *)>(
                     &clang_Cursor_isExternalSymbol),
                 R"_pb11_weaver(/**
 * Returns non-zero if the given cursor points to a symbol marked with
 * external_source_symbol attribute.
 *
 * \param language If non-NULL, and the attribute is present, will be set to
 * the 'language' string from the attribute.
 *
 * \param definedIn If non-NULL, and the attribute is present, will be set to
 * the 'definedIn' string from the attribute.
 *
 * \param isGenerated If non-NULL, and the attribute is present, will be set to
 * non-zero if the 'generated_declaration' is set in the attribute.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_isExternalSymbol"; }
};

struct Entity_clang_Cursor_isExternalSymbol
    : public Bind_clang_Cursor_isExternalSymbol<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_isExternalSymbol(EntityScope parent_h)
      : Bind_clang_Cursor_isExternalSymbol<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_isExternalSymbol

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getCommentRange
template <class Pybind11T>
struct Bind_clang_Cursor_getCommentRange : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getCommentRange(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getCommentRange",
        static_cast<CXSourceRange (*)(CXCursor)>(&clang_Cursor_getCommentRange),
        R"_pb11_weaver(/**
 * Given a cursor that represents a declaration, return the associated
 * comment's source range.  The range may include multiple consecutive comments
 * with whitespace in between.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getCommentRange"; }
};

struct Entity_clang_Cursor_getCommentRange
    : public Bind_clang_Cursor_getCommentRange<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getCommentRange(EntityScope parent_h)
      : Bind_clang_Cursor_getCommentRange<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getCommentRange

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getRawCommentText
template <class Pybind11T>
struct Bind_clang_Cursor_getRawCommentText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getRawCommentText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getRawCommentText",
        static_cast<CXString (*)(CXCursor)>(&clang_Cursor_getRawCommentText),
        R"_pb11_weaver(/**
 * Given a cursor that represents a declaration, return the associated
 * comment text, including comment markers.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getRawCommentText"; }
};

struct Entity_clang_Cursor_getRawCommentText
    : public Bind_clang_Cursor_getRawCommentText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getRawCommentText(EntityScope parent_h)
      : Bind_clang_Cursor_getRawCommentText<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getRawCommentText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getBriefCommentText
template <class Pybind11T>
struct Bind_clang_Cursor_getBriefCommentText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getBriefCommentText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getBriefCommentText",
        static_cast<CXString (*)(CXCursor)>(&clang_Cursor_getBriefCommentText),
        R"_pb11_weaver(/**
 * Given a cursor that represents a documentable entity (e.g.,
 * declaration), return the associated \paragraph; otherwise return the
 * first paragraph.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getBriefCommentText"; }
};

struct Entity_clang_Cursor_getBriefCommentText
    : public Bind_clang_Cursor_getBriefCommentText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getBriefCommentText(EntityScope parent_h)
      : Bind_clang_Cursor_getBriefCommentText<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getBriefCommentText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getMangling
template <class Pybind11T>
struct Bind_clang_Cursor_getMangling : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getMangling(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Cursor_getMangling",
                 static_cast<CXString (*)(CXCursor)>(&clang_Cursor_getMangling),
                 R"_pb11_weaver(/**
 * Retrieve the CXString representing the mangled name of the cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getMangling"; }
};

struct Entity_clang_Cursor_getMangling
    : public Bind_clang_Cursor_getMangling<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getMangling(EntityScope parent_h)
      : Bind_clang_Cursor_getMangling<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getMangling

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getCXXManglings
template <class Pybind11T>
struct Bind_clang_Cursor_getCXXManglings : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getCXXManglings(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getCXXManglings",
        static_cast<CXStringSet *(*)(CXCursor)>(&clang_Cursor_getCXXManglings),
        R"_pb11_weaver(/**
 * Retrieve the CXStrings representing the mangled symbols of the C++
 * constructor or destructor at the cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getCXXManglings"; }
};

struct Entity_clang_Cursor_getCXXManglings
    : public Bind_clang_Cursor_getCXXManglings<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getCXXManglings(EntityScope parent_h)
      : Bind_clang_Cursor_getCXXManglings<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getCXXManglings

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCManglings
template <class Pybind11T>
struct Bind_clang_Cursor_getObjCManglings : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getObjCManglings(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getObjCManglings",
        static_cast<CXStringSet *(*)(CXCursor)>(&clang_Cursor_getObjCManglings),
        R"_pb11_weaver(/**
 * Retrieve the CXStrings representing the mangled symbols of the ObjC
 * class interface or implementation at the cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getObjCManglings"; }
};

struct Entity_clang_Cursor_getObjCManglings
    : public Bind_clang_Cursor_getObjCManglings<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getObjCManglings(EntityScope parent_h)
      : Bind_clang_Cursor_getObjCManglings<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getObjCManglings

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getModule
template <class Pybind11T>
struct Bind_clang_Cursor_getModule : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getModule(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getModule",
        [=](CXCursor C) {
          return pybind11_weaver::WrapP<void *>(clang_Cursor_getModule(C))
              .release();
        },
        R"_pb11_weaver(/**
 * Given a CXCursor_ModuleImportDecl cursor, return the associated module.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getModule"; }
};

struct Entity_clang_Cursor_getModule
    : public Bind_clang_Cursor_getModule<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getModule(EntityScope parent_h)
      : Bind_clang_Cursor_getModule<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getModule

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getModuleForFile
template <class Pybind11T>
struct Bind_clang_getModuleForFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getModuleForFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getModuleForFile",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0,
            pybind11_weaver::PointerWrapper<void *> *arg1) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getModuleForFile((arg0)->ptr, (arg1)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * Given a CXFile header file, return the module that contains it, if one
 * exists.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getModuleForFile"; }
};

struct Entity_clang_getModuleForFile
    : public Bind_clang_getModuleForFile<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getModuleForFile(EntityScope parent_h)
      : Bind_clang_getModuleForFile<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getModuleForFile

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Module_getASTFile
template <class Pybind11T>
struct Bind_clang_Module_getASTFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Module_getASTFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Module_getASTFile",
        [=](pybind11_weaver::PointerWrapper<void *> *Module) {
          return pybind11_weaver::WrapP<void *>(
                     clang_Module_getASTFile((Module)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * \param Module a module object.
 *
 * \returns the module file where the provided module object came from.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Module_getASTFile"; }
};

struct Entity_clang_Module_getASTFile
    : public Bind_clang_Module_getASTFile<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Module_getASTFile(EntityScope parent_h)
      : Bind_clang_Module_getASTFile<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Module_getASTFile

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Module_getParent
template <class Pybind11T>
struct Bind_clang_Module_getParent : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Module_getParent(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Module_getParent",
        [=](pybind11_weaver::PointerWrapper<void *> *Module) {
          return pybind11_weaver::WrapP<void *>(
                     clang_Module_getParent((Module)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * \param Module a module object.
 *
 * \returns the parent of a sub-module or NULL if the given module is top-level,
 * e.g. for 'std.vector' it will return the 'std' module.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Module_getParent"; }
};

struct Entity_clang_Module_getParent
    : public Bind_clang_Module_getParent<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Module_getParent(EntityScope parent_h)
      : Bind_clang_Module_getParent<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Module_getParent

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Module_getName
template <class Pybind11T>
struct Bind_clang_Module_getName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Module_getName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Module_getName",
        [=](pybind11_weaver::PointerWrapper<void *> *Module) {
          return clang_Module_getName((Module)->ptr);
        },
        R"_pb11_weaver(/**
 * \param Module a module object.
 *
 * \returns the name of the module, e.g. for the 'std.vector' sub-module it
 * will return "vector".
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Module_getName"; }
};

struct Entity_clang_Module_getName
    : public Bind_clang_Module_getName<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Module_getName(EntityScope parent_h)
      : Bind_clang_Module_getName<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Module_getName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Module_getFullName
template <class Pybind11T>
struct Bind_clang_Module_getFullName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Module_getFullName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Module_getFullName",
        [=](pybind11_weaver::PointerWrapper<void *> *Module) {
          return clang_Module_getFullName((Module)->ptr);
        },
        R"_pb11_weaver(/**
 * \param Module a module object.
 *
 * \returns the full name of the module, e.g. "std.vector".
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Module_getFullName"; }
};

struct Entity_clang_Module_getFullName
    : public Bind_clang_Module_getFullName<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Module_getFullName(EntityScope parent_h)
      : Bind_clang_Module_getFullName<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Module_getFullName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Module_isSystem
template <class Pybind11T>
struct Bind_clang_Module_isSystem : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Module_isSystem(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Module_isSystem",
        [=](pybind11_weaver::PointerWrapper<void *> *Module) {
          return clang_Module_isSystem((Module)->ptr);
        },
        R"_pb11_weaver(/**
 * \param Module a module object.
 *
 * \returns non-zero if the module is a system one.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Module_isSystem"; }
};

struct Entity_clang_Module_isSystem
    : public Bind_clang_Module_isSystem<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Module_isSystem(EntityScope parent_h)
      : Bind_clang_Module_isSystem<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Module_isSystem

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Module_getNumTopLevelHeaders
template <class Pybind11T>
struct Bind_clang_Module_getNumTopLevelHeaders : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Module_getNumTopLevelHeaders(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Module_getNumTopLevelHeaders",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0,
            pybind11_weaver::PointerWrapper<void *> *Module) {
          return clang_Module_getNumTopLevelHeaders((arg0)->ptr, (Module)->ptr);
        },
        R"_pb11_weaver(/**
 * \param Module a module object.
 *
 * \returns the number of top level headers associated with this module.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Module_getNumTopLevelHeaders"; }
};

struct Entity_clang_Module_getNumTopLevelHeaders
    : public Bind_clang_Module_getNumTopLevelHeaders<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Module_getNumTopLevelHeaders(EntityScope parent_h)
      : Bind_clang_Module_getNumTopLevelHeaders<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Module_getNumTopLevelHeaders

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Module_getTopLevelHeader
template <class Pybind11T>
struct Bind_clang_Module_getTopLevelHeader : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Module_getTopLevelHeader(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Module_getTopLevelHeader",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0,
            pybind11_weaver::PointerWrapper<void *> *Module,
            unsigned int Index) {
          return pybind11_weaver::WrapP<void *>(
                     clang_Module_getTopLevelHeader((arg0)->ptr, (Module)->ptr,
                                                    Index))
              .release();
        },
        R"_pb11_weaver(/**
 * \param Module a module object.
 *
 * \param Index top level header index (zero-based).
 *
 * \returns the specified top level header associated with the module.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Module_getTopLevelHeader"; }
};

struct Entity_clang_Module_getTopLevelHeader
    : public Bind_clang_Module_getTopLevelHeader<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Module_getTopLevelHeader(EntityScope parent_h)
      : Bind_clang_Module_getTopLevelHeader<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Module_getTopLevelHeader

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXConstructor_isConvertingConstructor
template <class Pybind11T>
struct Bind_clang_CXXConstructor_isConvertingConstructor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXConstructor_isConvertingConstructor(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_CXXConstructor_isConvertingConstructor",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_CXXConstructor_isConvertingConstructor),
                 R"_pb11_weaver(/**
 * Determine if a C++ constructor is a converting constructor.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CXXConstructor_isConvertingConstructor";
  }
};

struct Entity_clang_CXXConstructor_isConvertingConstructor
    : public Bind_clang_CXXConstructor_isConvertingConstructor<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXConstructor_isConvertingConstructor(
      EntityScope parent_h)
      : Bind_clang_CXXConstructor_isConvertingConstructor<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXConstructor_isConvertingConstructor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXConstructor_isCopyConstructor
template <class Pybind11T>
struct Bind_clang_CXXConstructor_isCopyConstructor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXConstructor_isCopyConstructor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_CXXConstructor_isCopyConstructor",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_CXXConstructor_isCopyConstructor),
                 R"_pb11_weaver(/**
 * Determine if a C++ constructor is a copy constructor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXConstructor_isCopyConstructor"; }
};

struct Entity_clang_CXXConstructor_isCopyConstructor
    : public Bind_clang_CXXConstructor_isCopyConstructor<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXConstructor_isCopyConstructor(EntityScope parent_h)
      : Bind_clang_CXXConstructor_isCopyConstructor<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXConstructor_isCopyConstructor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXConstructor_isDefaultConstructor
template <class Pybind11T>
struct Bind_clang_CXXConstructor_isDefaultConstructor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXConstructor_isDefaultConstructor(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_CXXConstructor_isDefaultConstructor",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_CXXConstructor_isDefaultConstructor),
                 R"_pb11_weaver(/**
 * Determine if a C++ constructor is the default constructor.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CXXConstructor_isDefaultConstructor";
  }
};

struct Entity_clang_CXXConstructor_isDefaultConstructor
    : public Bind_clang_CXXConstructor_isDefaultConstructor<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXConstructor_isDefaultConstructor(
      EntityScope parent_h)
      : Bind_clang_CXXConstructor_isDefaultConstructor<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXConstructor_isDefaultConstructor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXConstructor_isMoveConstructor
template <class Pybind11T>
struct Bind_clang_CXXConstructor_isMoveConstructor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXConstructor_isMoveConstructor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_CXXConstructor_isMoveConstructor",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_CXXConstructor_isMoveConstructor),
                 R"_pb11_weaver(/**
 * Determine if a C++ constructor is a move constructor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXConstructor_isMoveConstructor"; }
};

struct Entity_clang_CXXConstructor_isMoveConstructor
    : public Bind_clang_CXXConstructor_isMoveConstructor<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXConstructor_isMoveConstructor(EntityScope parent_h)
      : Bind_clang_CXXConstructor_isMoveConstructor<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXConstructor_isMoveConstructor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXField_isMutable
template <class Pybind11T>
struct Bind_clang_CXXField_isMutable : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXField_isMutable(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXXField_isMutable",
        static_cast<unsigned int (*)(CXCursor)>(&clang_CXXField_isMutable),
        R"_pb11_weaver(/**
 * Determine if a C++ field is declared 'mutable'.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXField_isMutable"; }
};

struct Entity_clang_CXXField_isMutable
    : public Bind_clang_CXXField_isMutable<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXField_isMutable(EntityScope parent_h)
      : Bind_clang_CXXField_isMutable<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXField_isMutable

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isDefaulted
template <class Pybind11T>
struct Bind_clang_CXXMethod_isDefaulted : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXMethod_isDefaulted(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXXMethod_isDefaulted",
        static_cast<unsigned int (*)(CXCursor)>(&clang_CXXMethod_isDefaulted),
        R"_pb11_weaver(/**
 * Determine if a C++ method is declared '= default'.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXMethod_isDefaulted"; }
};

struct Entity_clang_CXXMethod_isDefaulted
    : public Bind_clang_CXXMethod_isDefaulted<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXMethod_isDefaulted(EntityScope parent_h)
      : Bind_clang_CXXMethod_isDefaulted<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isDefaulted

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isDeleted
template <class Pybind11T>
struct Bind_clang_CXXMethod_isDeleted : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXMethod_isDeleted(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXXMethod_isDeleted",
        static_cast<unsigned int (*)(CXCursor)>(&clang_CXXMethod_isDeleted),
        R"_pb11_weaver(/**
 * Determine if a C++ method is declared '= delete'.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXMethod_isDeleted"; }
};

struct Entity_clang_CXXMethod_isDeleted
    : public Bind_clang_CXXMethod_isDeleted<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXMethod_isDeleted(EntityScope parent_h)
      : Bind_clang_CXXMethod_isDeleted<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isDeleted

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isPureVirtual
template <class Pybind11T>
struct Bind_clang_CXXMethod_isPureVirtual : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXMethod_isPureVirtual(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXXMethod_isPureVirtual",
        static_cast<unsigned int (*)(CXCursor)>(&clang_CXXMethod_isPureVirtual),
        R"_pb11_weaver(/**
 * Determine if a C++ member function or member function template is
 * pure virtual.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXMethod_isPureVirtual"; }
};

struct Entity_clang_CXXMethod_isPureVirtual
    : public Bind_clang_CXXMethod_isPureVirtual<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXMethod_isPureVirtual(EntityScope parent_h)
      : Bind_clang_CXXMethod_isPureVirtual<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isPureVirtual

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isStatic
template <class Pybind11T>
struct Bind_clang_CXXMethod_isStatic : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXMethod_isStatic(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXXMethod_isStatic",
        static_cast<unsigned int (*)(CXCursor)>(&clang_CXXMethod_isStatic),
        R"_pb11_weaver(/**
 * Determine if a C++ member function or member function template is
 * declared 'static'.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXMethod_isStatic"; }
};

struct Entity_clang_CXXMethod_isStatic
    : public Bind_clang_CXXMethod_isStatic<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXMethod_isStatic(EntityScope parent_h)
      : Bind_clang_CXXMethod_isStatic<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isStatic

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isVirtual
template <class Pybind11T>
struct Bind_clang_CXXMethod_isVirtual : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXMethod_isVirtual(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXXMethod_isVirtual",
        static_cast<unsigned int (*)(CXCursor)>(&clang_CXXMethod_isVirtual),
        R"_pb11_weaver(/**
 * Determine if a C++ member function or member function template is
 * explicitly declared 'virtual' or if it overrides a virtual method from
 * one of the base classes.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXMethod_isVirtual"; }
};

struct Entity_clang_CXXMethod_isVirtual
    : public Bind_clang_CXXMethod_isVirtual<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXMethod_isVirtual(EntityScope parent_h)
      : Bind_clang_CXXMethod_isVirtual<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isVirtual

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isCopyAssignmentOperator
template <class Pybind11T>
struct Bind_clang_CXXMethod_isCopyAssignmentOperator : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXMethod_isCopyAssignmentOperator(EntityScope parent_h) {
  }

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_CXXMethod_isCopyAssignmentOperator",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_CXXMethod_isCopyAssignmentOperator),
                 R"_pb11_weaver(/**
 * Determine if a C++ member function is a copy-assignment operator,
 * returning 1 if such is the case and 0 otherwise.
 *
 * > A copy-assignment operator `X::operator=` is a non-static,
 * > non-template member function of _class_ `X` with exactly one
 * > parameter of type `X`, `X&`, `const X&`, `volatile X&` or `const
 * > volatile X&`.
 *
 * That is, for example, the `operator=` in:
 *
 *    class Foo {
 *        bool operator=(const volatile Foo&);
 *    };
 *
 * Is a copy-assignment operator, while the `operator=` in:
 *
 *    class Bar {
 *        bool operator=(const int&);
 *    };
 *
 * Is not.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CXXMethod_isCopyAssignmentOperator";
  }
};

struct Entity_clang_CXXMethod_isCopyAssignmentOperator
    : public Bind_clang_CXXMethod_isCopyAssignmentOperator<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXMethod_isCopyAssignmentOperator(EntityScope parent_h)
      : Bind_clang_CXXMethod_isCopyAssignmentOperator<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isCopyAssignmentOperator

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isMoveAssignmentOperator
template <class Pybind11T>
struct Bind_clang_CXXMethod_isMoveAssignmentOperator : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXMethod_isMoveAssignmentOperator(EntityScope parent_h) {
  }

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_CXXMethod_isMoveAssignmentOperator",
                 static_cast<unsigned int (*)(CXCursor)>(
                     &clang_CXXMethod_isMoveAssignmentOperator),
                 R"_pb11_weaver(/**
 * Determine if a C++ member function is a move-assignment operator,
 * returning 1 if such is the case and 0 otherwise.
 *
 * > A move-assignment operator `X::operator=` is a non-static,
 * > non-template member function of _class_ `X` with exactly one
 * > parameter of type `X&&`, `const X&&`, `volatile X&&` or `const
 * > volatile X&&`.
 *
 * That is, for example, the `operator=` in:
 *
 *    class Foo {
 *        bool operator=(const volatile Foo&&);
 *    };
 *
 * Is a move-assignment operator, while the `operator=` in:
 *
 *    class Bar {
 *        bool operator=(const int&&);
 *    };
 *
 * Is not.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_CXXMethod_isMoveAssignmentOperator";
  }
};

struct Entity_clang_CXXMethod_isMoveAssignmentOperator
    : public Bind_clang_CXXMethod_isMoveAssignmentOperator<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXMethod_isMoveAssignmentOperator(EntityScope parent_h)
      : Bind_clang_CXXMethod_isMoveAssignmentOperator<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isMoveAssignmentOperator

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXRecord_isAbstract
template <class Pybind11T>
struct Bind_clang_CXXRecord_isAbstract : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXRecord_isAbstract(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXXRecord_isAbstract",
        static_cast<unsigned int (*)(CXCursor)>(&clang_CXXRecord_isAbstract),
        R"_pb11_weaver(/**
 * Determine if a C++ record is abstract, i.e. whether a class or struct
 * has a pure virtual member function.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXRecord_isAbstract"; }
};

struct Entity_clang_CXXRecord_isAbstract
    : public Bind_clang_CXXRecord_isAbstract<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXRecord_isAbstract(EntityScope parent_h)
      : Bind_clang_CXXRecord_isAbstract<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXRecord_isAbstract

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EnumDecl_isScoped
template <class Pybind11T>
struct Bind_clang_EnumDecl_isScoped : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EnumDecl_isScoped(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EnumDecl_isScoped",
        static_cast<unsigned int (*)(CXCursor)>(&clang_EnumDecl_isScoped),
        R"_pb11_weaver(/**
 * Determine if an enum declaration refers to a scoped enum.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EnumDecl_isScoped"; }
};

struct Entity_clang_EnumDecl_isScoped
    : public Bind_clang_EnumDecl_isScoped<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EnumDecl_isScoped(EntityScope parent_h)
      : Bind_clang_EnumDecl_isScoped<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EnumDecl_isScoped

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isConst
template <class Pybind11T>
struct Bind_clang_CXXMethod_isConst : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXXMethod_isConst(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXXMethod_isConst",
        static_cast<unsigned int (*)(CXCursor)>(&clang_CXXMethod_isConst),
        R"_pb11_weaver(/**
 * Determine if a C++ member function or member function template is
 * declared 'const'.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXXMethod_isConst"; }
};

struct Entity_clang_CXXMethod_isConst
    : public Bind_clang_CXXMethod_isConst<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXXMethod_isConst(EntityScope parent_h)
      : Bind_clang_CXXMethod_isConst<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXXMethod_isConst

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTemplateCursorKind
template <class Pybind11T>
struct Bind_clang_getTemplateCursorKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTemplateCursorKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTemplateCursorKind",
        static_cast<CXCursorKind (*)(CXCursor)>(&clang_getTemplateCursorKind),
        R"_pb11_weaver(/**
 * Given a cursor that represents a template, determine
 * the cursor kind of the specializations would be generated by instantiating
 * the template.
 *
 * This routine can be used to determine what flavor of function template,
 * class template, or class template partial specialization is stored in the
 * cursor. For example, it can describe whether a class template cursor is
 * declared with "struct", "class" or "union".
 *
 * \param C The cursor to query. This cursor should represent a template
 * declaration.
 *
 * \returns The cursor kind of the specializations that would be generated
 * by instantiating the template \p C. If \p C is not a template, returns
 * \c CXCursor_NoDeclFound.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTemplateCursorKind"; }
};

struct Entity_clang_getTemplateCursorKind
    : public Bind_clang_getTemplateCursorKind<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTemplateCursorKind(EntityScope parent_h)
      : Bind_clang_getTemplateCursorKind<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTemplateCursorKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getSpecializedCursorTemplate
template <class Pybind11T>
struct Bind_clang_getSpecializedCursorTemplate : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getSpecializedCursorTemplate(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getSpecializedCursorTemplate",
                 static_cast<CXCursor (*)(CXCursor)>(
                     &clang_getSpecializedCursorTemplate),
                 R"_pb11_weaver(/**
 * Given a cursor that may represent a specialization or instantiation
 * of a template, retrieve the cursor that represents the template that it
 * specializes or from which it was instantiated.
 *
 * This routine determines the template involved both for explicit
 * specializations of templates and for implicit instantiations of the template,
 * both of which are referred to as "specializations". For a class template
 * specialization (e.g., \c std::vector<bool>), this routine will return
 * either the primary template (\c std::vector) or, if the specialization was
 * instantiated from a class template partial specialization, the class template
 * partial specialization. For a class template partial specialization and a
 * function template specialization (including instantiations), this
 * this routine will return the specialized template.
 *
 * For members of a class template (e.g., member functions, member classes, or
 * static data members), returns the specialized or instantiated member.
 * Although not strictly "templates" in the C++ language, members of class
 * templates have the same notions of specializations and instantiations that
 * templates do, so this routine treats them similarly.
 *
 * \param C A cursor that may be a specialization of a template or a member
 * of a template.
 *
 * \returns If the given cursor is a specialization or instantiation of a
 * template or a member thereof, the template or member that it specializes or
 * from which it was instantiated. Otherwise, returns a NULL cursor.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getSpecializedCursorTemplate"; }
};

struct Entity_clang_getSpecializedCursorTemplate
    : public Bind_clang_getSpecializedCursorTemplate<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getSpecializedCursorTemplate(EntityScope parent_h)
      : Bind_clang_getSpecializedCursorTemplate<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getSpecializedCursorTemplate

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorReferenceNameRange
template <class Pybind11T>
struct Bind_clang_getCursorReferenceNameRange : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorReferenceNameRange(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorReferenceNameRange",
        static_cast<CXSourceRange (*)(CXCursor, unsigned int, unsigned int)>(
            &clang_getCursorReferenceNameRange),
        R"_pb11_weaver(/**
 * Given a cursor that references something else, return the source range
 * covering that reference.
 *
 * \param C A cursor pointing to a member reference, a declaration reference, or
 * an operator call.
 * \param NameFlags A bitset with three independent flags:
 * CXNameRange_WantQualifier, CXNameRange_WantTemplateArgs, and
 * CXNameRange_WantSinglePiece.
 * \param PieceIndex For contiguous names or when passing the flag
 * CXNameRange_WantSinglePiece, only one piece with index 0 is
 * available. When the CXNameRange_WantSinglePiece flag is not passed for a
 * non-contiguous names, this index can be used to retrieve the individual
 * pieces of the name. See also CXNameRange_WantSinglePiece.
 *
 * \returns The piece of the name pointed to by the given cursor. If there is no
 * name, or if the PieceIndex is out-of-range, a null-cursor will be returned.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorReferenceNameRange"; }
};

struct Entity_clang_getCursorReferenceNameRange
    : public Bind_clang_getCursorReferenceNameRange<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorReferenceNameRange(EntityScope parent_h)
      : Bind_clang_getCursorReferenceNameRange<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorReferenceNameRange

#ifndef PB11_WEAVER_DISABLE_Entity_CXNameRefFlags
template <class Pybind11T> struct Bind_CXNameRefFlags : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXNameRefFlags(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXNameRange_WantQualifier",
                   CXNameRefFlags::CXNameRange_WantQualifier, R"_pb11_weaver(/**
   * Include the nested-name-specifier, e.g. Foo:: in x.Foo::y, in the
   * range.
   */)_pb11_weaver");
    pb11_obj.value("CXNameRange_WantTemplateArgs",
                   CXNameRefFlags::CXNameRange_WantTemplateArgs,
                   R"_pb11_weaver(/**
   * Include the explicit template arguments, e.g. \<int> in x.f<int>,
   * in the range.
   */)_pb11_weaver");
    pb11_obj.value("CXNameRange_WantSinglePiece",
                   CXNameRefFlags::CXNameRange_WantSinglePiece,
                   R"_pb11_weaver(/**
   * If the name is non-contiguous, return the full spanning range.
   *
   * Non-contiguous names occur in Objective-C when a selector with two or more
   * parameters is used, or in C++ when using an operator:
   * \code
   * [object doSomething:here withValue:there]; // Objective-C
   * return some_vector[1]; // C++
   * \endcode
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXNameRefFlags"; }
};

struct Entity_CXNameRefFlags
    : public Bind_CXNameRefFlags<
          std::decay_t<pybind11::enum_<CXNameRefFlags>>> {

  explicit Entity_CXNameRefFlags(EntityScope parent_h)
      : Bind_CXNameRefFlags<std::decay_t<pybind11::enum_<CXNameRefFlags>>>(
            parent_h),
        handle{parent_h, "CXNameRefFlags", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXNameRefFlags> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXNameRefFlags

#ifndef PB11_WEAVER_DISABLE_Entity_CXTokenKind
template <class Pybind11T> struct Bind_CXTokenKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXTokenKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXToken_Punctuation", CXTokenKind::CXToken_Punctuation,
                   R"_pb11_weaver(/**
   * A token that contains some kind of punctuation.
   */)_pb11_weaver");
    pb11_obj.value("CXToken_Keyword", CXTokenKind::CXToken_Keyword,
                   R"_pb11_weaver(/**
   * A language keyword.
   */)_pb11_weaver");
    pb11_obj.value("CXToken_Identifier", CXTokenKind::CXToken_Identifier,
                   R"_pb11_weaver(/**
   * An identifier (that is not a keyword).
   */)_pb11_weaver");
    pb11_obj.value("CXToken_Literal", CXTokenKind::CXToken_Literal,
                   R"_pb11_weaver(/**
   * A numeric, string, or character literal.
   */)_pb11_weaver");
    pb11_obj.value("CXToken_Comment", CXTokenKind::CXToken_Comment,
                   R"_pb11_weaver(/**
   * A comment.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXTokenKind"; }
};

struct Entity_CXTokenKind
    : public Bind_CXTokenKind<std::decay_t<pybind11::enum_<CXTokenKind>>> {

  explicit Entity_CXTokenKind(EntityScope parent_h)
      : Bind_CXTokenKind<std::decay_t<pybind11::enum_<CXTokenKind>>>(parent_h),
        handle{parent_h, "CXTokenKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes a kind of token.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXTokenKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXTokenKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXToken
template <class Pybind11T> struct Bind_CXToken : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXToken(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXToken"; }
};

struct Entity_CXToken
    : public Bind_CXToken<std::decay_t<pybind11::class_<CXToken>>> {

  explicit Entity_CXToken(EntityScope parent_h)
      : Bind_CXToken<std::decay_t<pybind11::class_<CXToken>>>(parent_h),
        handle{parent_h, "CXToken", pybind11::dynamic_attr(), R"_pb11_weaver(/**
 * Describes a single preprocessing token.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXToken> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXToken

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getToken
template <class Pybind11T> struct Bind_clang_getToken : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getToken(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getToken",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU,
            CXSourceLocation Location) {
          return clang_getToken((TU)->ptr, Location);
        },
        R"_pb11_weaver(/**
 * Get the raw lexical token starting with the given location.
 *
 * \param TU the translation unit whose text is being tokenized.
 *
 * \param Location the source location with which the token starts.
 *
 * \returns The token starting with the given location or NULL if no such token
 * exist. The returned pointer must be freed with clang_disposeTokens before the
 * translation unit is destroyed.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getToken"; }
};

struct Entity_clang_getToken
    : public Bind_clang_getToken<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getToken(EntityScope parent_h)
      : Bind_clang_getToken<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getToken

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTokenKind
template <class Pybind11T> struct Bind_clang_getTokenKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTokenKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getTokenKind",
                 static_cast<CXTokenKind (*)(CXToken)>(&clang_getTokenKind),
                 R"_pb11_weaver(/**
 * Determine the kind of the given token.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTokenKind"; }
};

struct Entity_clang_getTokenKind
    : public Bind_clang_getTokenKind<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTokenKind(EntityScope parent_h)
      : Bind_clang_getTokenKind<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTokenKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTokenSpelling
template <class Pybind11T>
struct Bind_clang_getTokenSpelling : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTokenSpelling(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTokenSpelling",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0,
            CXToken arg1) { return clang_getTokenSpelling((arg0)->ptr, arg1); },
        R"_pb11_weaver(/**
 * Determine the spelling of the given token.
 *
 * The spelling of a token is the textual representation of that token, e.g.,
 * the text of an identifier or keyword.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTokenSpelling"; }
};

struct Entity_clang_getTokenSpelling
    : public Bind_clang_getTokenSpelling<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTokenSpelling(EntityScope parent_h)
      : Bind_clang_getTokenSpelling<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTokenSpelling

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTokenLocation
template <class Pybind11T>
struct Bind_clang_getTokenLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTokenLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTokenLocation",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0,
            CXToken arg1) { return clang_getTokenLocation((arg0)->ptr, arg1); },
        R"_pb11_weaver(/**
 * Retrieve the source location of the given token.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTokenLocation"; }
};

struct Entity_clang_getTokenLocation
    : public Bind_clang_getTokenLocation<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTokenLocation(EntityScope parent_h)
      : Bind_clang_getTokenLocation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTokenLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getTokenExtent
template <class Pybind11T>
struct Bind_clang_getTokenExtent : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getTokenExtent(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getTokenExtent",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg0,
            CXToken arg1) { return clang_getTokenExtent((arg0)->ptr, arg1); },
        R"_pb11_weaver(/**
 * Retrieve a source range that covers the given token.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getTokenExtent"; }
};

struct Entity_clang_getTokenExtent
    : public Bind_clang_getTokenExtent<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getTokenExtent(EntityScope parent_h)
      : Bind_clang_getTokenExtent<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getTokenExtent

#ifndef PB11_WEAVER_DISABLE_Entity_clang_tokenize
template <class Pybind11T> struct Bind_clang_tokenize : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_tokenize(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_tokenize",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU,
            CXSourceRange Range,
            pybind11_weaver::PointerWrapper<CXToken **> *Tokens,
            unsigned int *NumTokens) {
          return clang_tokenize((TU)->ptr, Range, (Tokens)->ptr, NumTokens);
        },
        R"_pb11_weaver(/**
 * Tokenize the source code described by the given range into raw
 * lexical tokens.
 *
 * \param TU the translation unit whose text is being tokenized.
 *
 * \param Range the source range in which text should be tokenized. All of the
 * tokens produced by tokenization will fall within this source range,
 *
 * \param Tokens this pointer will be set to point to the array of tokens
 * that occur within the given source range. The returned pointer must be
 * freed with clang_disposeTokens() before the translation unit is destroyed.
 *
 * \param NumTokens will be set to the number of tokens in the \c *Tokens
 * array.
 *
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_tokenize"; }
};

struct Entity_clang_tokenize
    : public Bind_clang_tokenize<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_tokenize(EntityScope parent_h)
      : Bind_clang_tokenize<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_tokenize

#ifndef PB11_WEAVER_DISABLE_Entity_clang_annotateTokens
template <class Pybind11T>
struct Bind_clang_annotateTokens : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_annotateTokens(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_annotateTokens",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU,
            CXToken *Tokens, unsigned int NumTokens, CXCursor *Cursors) {
          return clang_annotateTokens((TU)->ptr, Tokens, NumTokens, Cursors);
        },
        R"_pb11_weaver(/**
 * Annotate the given set of tokens by providing cursors for each token
 * that can be mapped to a specific entity within the abstract syntax tree.
 *
 * This token-annotation routine is equivalent to invoking
 * clang_getCursor() for the source locations of each of the
 * tokens. The cursors provided are filtered, so that only those
 * cursors that have a direct correspondence to the token are
 * accepted. For example, given a function call \c f(x),
 * clang_getCursor() would provide the following cursors:
 *
 *   * when the cursor is over the 'f', a DeclRefExpr cursor referring to 'f'.
 *   * when the cursor is over the '(' or the ')', a CallExpr referring to 'f'.
 *   * when the cursor is over the 'x', a DeclRefExpr cursor referring to 'x'.
 *
 * Only the first and last of these cursors will occur within the
 * annotate, since the tokens "f" and "x' directly refer to a function
 * and a variable, respectively, but the parentheses are just a small
 * part of the full syntax of the function call expression, which is
 * not provided as an annotation.
 *
 * \param TU the translation unit that owns the given tokens.
 *
 * \param Tokens the set of tokens to annotate.
 *
 * \param NumTokens the number of tokens in \p Tokens.
 *
 * \param Cursors an array of \p NumTokens cursors, whose contents will be
 * replaced with the cursors corresponding to each token.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_annotateTokens"; }
};

struct Entity_clang_annotateTokens
    : public Bind_clang_annotateTokens<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_annotateTokens(EntityScope parent_h)
      : Bind_clang_annotateTokens<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_annotateTokens

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeTokens
template <class Pybind11T> struct Bind_clang_disposeTokens : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeTokens(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_disposeTokens",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU,
            CXToken *Tokens, unsigned int NumTokens) {
          return clang_disposeTokens((TU)->ptr, Tokens, NumTokens);
        },
        R"_pb11_weaver(/**
 * Free the given set of tokens.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeTokens"; }
};

struct Entity_clang_disposeTokens
    : public Bind_clang_disposeTokens<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeTokens(EntityScope parent_h)
      : Bind_clang_disposeTokens<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeTokens

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorKindSpelling
template <class Pybind11T>
struct Bind_clang_getCursorKindSpelling : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorKindSpelling(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorKindSpelling",
        static_cast<CXString (*)(CXCursorKind)>(&clang_getCursorKindSpelling),
        R"_pb11_weaver(/* for debug/testing */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorKindSpelling"; }
};

struct Entity_clang_getCursorKindSpelling
    : public Bind_clang_getCursorKindSpelling<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorKindSpelling(EntityScope parent_h)
      : Bind_clang_getCursorKindSpelling<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorKindSpelling

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getDefinitionSpellingAndExtent
template <class Pybind11T>
struct Bind_clang_getDefinitionSpellingAndExtent : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getDefinitionSpellingAndExtent(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getDefinitionSpellingAndExtent",
                 [=](CXCursor arg0,
                     pybind11_weaver::PointerWrapper<const char **> *startBuf,
                     pybind11_weaver::PointerWrapper<const char **> *endBuf,
                     unsigned int *startLine, unsigned int *startColumn,
                     unsigned int *endLine, unsigned int *endColumn) {
                   return clang_getDefinitionSpellingAndExtent(
                       arg0, (startBuf)->ptr, (endBuf)->ptr, startLine,
                       startColumn, endLine, endColumn);
                 });
  }

  static const char *Key() { return "clang_getDefinitionSpellingAndExtent"; }
};

struct Entity_clang_getDefinitionSpellingAndExtent
    : public Bind_clang_getDefinitionSpellingAndExtent<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getDefinitionSpellingAndExtent(EntityScope parent_h)
      : Bind_clang_getDefinitionSpellingAndExtent<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getDefinitionSpellingAndExtent

#ifndef PB11_WEAVER_DISABLE_Entity_clang_enableStackTraces
template <class Pybind11T>
struct Bind_clang_enableStackTraces : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_enableStackTraces(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_enableStackTraces",
                 static_cast<void (*)()>(&clang_enableStackTraces));
  }

  static const char *Key() { return "clang_enableStackTraces"; }
};

struct Entity_clang_enableStackTraces
    : public Bind_clang_enableStackTraces<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_enableStackTraces(EntityScope parent_h)
      : Bind_clang_enableStackTraces<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_enableStackTraces

#ifndef PB11_WEAVER_DISABLE_Entity_clang_executeOnThread
template <class Pybind11T>
struct Bind_clang_executeOnThread : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_executeOnThread(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_executeOnThread",
        [=](std::function<void(pybind11_weaver::PointerWrapper<void *> *)> fn,
            pybind11_weaver::PointerWrapper<void *> *user_data,
            unsigned int stack_size) {
          return clang_executeOnThread(
              pybind11_weaver::FnPointerWrapper<
                  void, pybind11_weaver::PointerWrapper<void *> *>::
                  GetCptr<void, void *>::Run(
                      (fn), pybind11_weaver::Guardian(),
                      [](void *arg0_0) {
                        auto &to_call = pybind11_weaver::FnPointerWrapper<
                            void, pybind11_weaver::PointerWrapper<void *>
                                      *>::FnProxy(2);
                        return to_call(
                            pybind11_weaver::WrapP<void *>(arg0_0).get());
                      },
                      2),
              (user_data)->ptr, stack_size);
        });
  }

  static const char *Key() { return "clang_executeOnThread"; }
};

struct Entity_clang_executeOnThread
    : public Bind_clang_executeOnThread<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_executeOnThread(EntityScope parent_h)
      : Bind_clang_executeOnThread<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_executeOnThread

#ifndef PB11_WEAVER_DISABLE_Entity_CXCompletionResult
template <class Pybind11T> struct Bind_CXCompletionResult : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCompletionResult(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * The code-completion string that describes how to insert this
   * code-completion result into the editing buffer.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("CursorKind", &CXCompletionResult::CursorKind,
                           R"_pb11_weaver(/**
   * The kind of entity that this completion refers to.
   *
   * The cursor kind will be a macro, keyword, or a declaration (one of the
   * *Decl cursor kinds), describing the entity that the completion is
   * referring to.
   *
   * \todo In the future, we would like to provide a full cursor, to allow
   * the client to extract additional information from declaration.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXCompletionResult"; }
};

struct Entity_CXCompletionResult
    : public Bind_CXCompletionResult<
          std::decay_t<pybind11::class_<CXCompletionResult>>> {

  explicit Entity_CXCompletionResult(EntityScope parent_h)
      : Bind_CXCompletionResult<
            std::decay_t<pybind11::class_<CXCompletionResult>>>(parent_h),
        handle{parent_h, "CXCompletionResult", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * A single result of code completion.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXCompletionResult> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCompletionResult

#ifndef PB11_WEAVER_DISABLE_Entity_CXCompletionChunkKind
template <class Pybind11T>
struct Bind_CXCompletionChunkKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCompletionChunkKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCompletionChunk_Optional",
                   CXCompletionChunkKind::CXCompletionChunk_Optional,
                   R"_pb11_weaver(/**
   * A code-completion string that describes "optional" text that
   * could be a part of the template (but is not required).
   *
   * The Optional chunk is the only kind of chunk that has a code-completion
   * string for its representation, which is accessible via
   * \c clang_getCompletionChunkCompletionString(). The code-completion string
   * describes an additional part of the template that is completely optional.
   * For example, optional chunks can be used to describe the placeholders for
   * arguments that match up with defaulted function parameters, e.g. given:
   *
   * \code
   * void f(int x, float y = 3.14, double z = 2.71828);
   * \endcode
   *
   * The code-completion string for this function would contain:
   *   - a TypedText chunk for "f".
   *   - a LeftParen chunk for "(".
   *   - a Placeholder chunk for "int x"
   *   - an Optional chunk containing the remaining defaulted arguments, e.g.,
   *       - a Comma chunk for ","
   *       - a Placeholder chunk for "float y"
   *       - an Optional chunk containing the last defaulted argument:
   *           - a Comma chunk for ","
   *           - a Placeholder chunk for "double z"
   *   - a RightParen chunk for ")"
   *
   * There are many ways to handle Optional chunks. Two simple approaches are:
   *   - Completely ignore optional chunks, in which case the template for the
   *     function "f" would only include the first parameter ("int x").
   *   - Fully expand all optional chunks, in which case the template for the
   *     function "f" would have all of the parameters.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_TypedText",
                   CXCompletionChunkKind::CXCompletionChunk_TypedText,
                   R"_pb11_weaver(/**
   * Text that a user would be expected to type to get this
   * code-completion result.
   *
   * There will be exactly one "typed text" chunk in a semantic string, which
   * will typically provide the spelling of a keyword or the name of a
   * declaration that could be used at the current code point. Clients are
   * expected to filter the code-completion results based on the text in this
   * chunk.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_Text",
                   CXCompletionChunkKind::CXCompletionChunk_Text,
                   R"_pb11_weaver(/**
   * Text that should be inserted as part of a code-completion result.
   *
   * A "text" chunk represents text that is part of the template to be
   * inserted into user code should this particular code-completion result
   * be selected.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_Placeholder",
                   CXCompletionChunkKind::CXCompletionChunk_Placeholder,
                   R"_pb11_weaver(/**
   * Placeholder text that should be replaced by the user.
   *
   * A "placeholder" chunk marks a place where the user should insert text
   * into the code-completion template. For example, placeholders might mark
   * the function parameters for a function declaration, to indicate that the
   * user should provide arguments for each of those parameters. The actual
   * text in a placeholder is a suggestion for the text to display before
   * the user replaces the placeholder with real code.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_Informative",
                   CXCompletionChunkKind::CXCompletionChunk_Informative,
                   R"_pb11_weaver(/**
   * Informative text that should be displayed but never inserted as
   * part of the template.
   *
   * An "informative" chunk contains annotations that can be displayed to
   * help the user decide whether a particular code-completion result is the
   * right option, but which is not part of the actual template to be inserted
   * by code completion.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_CurrentParameter",
                   CXCompletionChunkKind::CXCompletionChunk_CurrentParameter,
                   R"_pb11_weaver(/**
   * Text that describes the current parameter when code-completion is
   * referring to function call, message send, or template specialization.
   *
   * A "current parameter" chunk occurs when code-completion is providing
   * information about a parameter corresponding to the argument at the
   * code-completion point. For example, given a function
   *
   * \code
   * int add(int x, int y);
   * \endcode
   *
   * and the source code \c add(, where the code-completion point is after the
   * "(", the code-completion string will contain a "current parameter" chunk
   * for "int x", indicating that the current argument will initialize that
   * parameter. After typing further, to \c add(17, (where the code-completion
   * point is after the ","), the code-completion string will contain a
   * "current parameter" chunk to "int y".
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_LeftParen",
                   CXCompletionChunkKind::CXCompletionChunk_LeftParen,
                   R"_pb11_weaver(/**
   * A left parenthesis ('('), used to initiate a function call or
   * signal the beginning of a function parameter list.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_RightParen",
                   CXCompletionChunkKind::CXCompletionChunk_RightParen,
                   R"_pb11_weaver(/**
   * A right parenthesis (')'), used to finish a function call or
   * signal the end of a function parameter list.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_LeftBracket",
                   CXCompletionChunkKind::CXCompletionChunk_LeftBracket,
                   R"_pb11_weaver(/**
   * A left bracket ('[').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_RightBracket",
                   CXCompletionChunkKind::CXCompletionChunk_RightBracket,
                   R"_pb11_weaver(/**
   * A right bracket (']').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_LeftBrace",
                   CXCompletionChunkKind::CXCompletionChunk_LeftBrace,
                   R"_pb11_weaver(/**
   * A left brace ('{').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_RightBrace",
                   CXCompletionChunkKind::CXCompletionChunk_RightBrace,
                   R"_pb11_weaver(/**
   * A right brace ('}').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_LeftAngle",
                   CXCompletionChunkKind::CXCompletionChunk_LeftAngle,
                   R"_pb11_weaver(/**
   * A left angle bracket ('<').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_RightAngle",
                   CXCompletionChunkKind::CXCompletionChunk_RightAngle,
                   R"_pb11_weaver(/**
   * A right angle bracket ('>').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_Comma",
                   CXCompletionChunkKind::CXCompletionChunk_Comma,
                   R"_pb11_weaver(/**
   * A comma separator (',').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_ResultType",
                   CXCompletionChunkKind::CXCompletionChunk_ResultType,
                   R"_pb11_weaver(/**
   * Text that specifies the result type of a given result.
   *
   * This special kind of informative chunk is not meant to be inserted into
   * the text buffer. Rather, it is meant to illustrate the type that an
   * expression using the given completion string would have.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_Colon",
                   CXCompletionChunkKind::CXCompletionChunk_Colon,
                   R"_pb11_weaver(/**
   * A colon (':').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_SemiColon",
                   CXCompletionChunkKind::CXCompletionChunk_SemiColon,
                   R"_pb11_weaver(/**
   * A semicolon (';').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_Equal",
                   CXCompletionChunkKind::CXCompletionChunk_Equal,
                   R"_pb11_weaver(/**
   * An '=' sign.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_HorizontalSpace",
                   CXCompletionChunkKind::CXCompletionChunk_HorizontalSpace,
                   R"_pb11_weaver(/**
   * Horizontal space (' ').
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionChunk_VerticalSpace",
                   CXCompletionChunkKind::CXCompletionChunk_VerticalSpace,
                   R"_pb11_weaver(/**
   * Vertical space ('\\n'), after which it is generally a good idea to
   * perform indentation.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCompletionChunkKind"; }
};

struct Entity_CXCompletionChunkKind
    : public Bind_CXCompletionChunkKind<
          std::decay_t<pybind11::enum_<CXCompletionChunkKind>>> {

  explicit Entity_CXCompletionChunkKind(EntityScope parent_h)
      : Bind_CXCompletionChunkKind<
            std::decay_t<pybind11::enum_<CXCompletionChunkKind>>>(parent_h),
        handle{parent_h, "CXCompletionChunkKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes a single piece of text within a code-completion string.
 *
 * Each "chunk" within a code-completion string (\c CXCompletionString) is
 * either a piece of text with a specific "kind" that describes how that text
 * should be interpreted by the client or is another completion string.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCompletionChunkKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCompletionChunkKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionChunkKind
template <class Pybind11T>
struct Bind_clang_getCompletionChunkKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionChunkKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionChunkKind",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string,
            unsigned int chunk_number) {
          return clang_getCompletionChunkKind((completion_string)->ptr,
                                              chunk_number);
        },
        R"_pb11_weaver(/**
 * Determine the kind of a particular chunk within a completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the kind of the chunk at the index \c chunk_number.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionChunkKind"; }
};

struct Entity_clang_getCompletionChunkKind
    : public Bind_clang_getCompletionChunkKind<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionChunkKind(EntityScope parent_h)
      : Bind_clang_getCompletionChunkKind<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionChunkKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionChunkText
template <class Pybind11T>
struct Bind_clang_getCompletionChunkText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionChunkText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionChunkText",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string,
            unsigned int chunk_number) {
          return clang_getCompletionChunkText((completion_string)->ptr,
                                              chunk_number);
        },
        R"_pb11_weaver(/**
 * Retrieve the text associated with a particular chunk within a
 * completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the text associated with the chunk at index \c chunk_number.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionChunkText"; }
};

struct Entity_clang_getCompletionChunkText
    : public Bind_clang_getCompletionChunkText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionChunkText(EntityScope parent_h)
      : Bind_clang_getCompletionChunkText<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionChunkText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionChunkCompletionString
template <class Pybind11T>
struct Bind_clang_getCompletionChunkCompletionString : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionChunkCompletionString(EntityScope parent_h) {
  }

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionChunkCompletionString",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string,
            unsigned int chunk_number) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getCompletionChunkCompletionString(
                         (completion_string)->ptr, chunk_number))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve the completion string associated with a particular chunk
 * within a completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the completion string associated with the chunk at index
 * \c chunk_number.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_getCompletionChunkCompletionString";
  }
};

struct Entity_clang_getCompletionChunkCompletionString
    : public Bind_clang_getCompletionChunkCompletionString<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionChunkCompletionString(EntityScope parent_h)
      : Bind_clang_getCompletionChunkCompletionString<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionChunkCompletionString

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getNumCompletionChunks
template <class Pybind11T>
struct Bind_clang_getNumCompletionChunks : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getNumCompletionChunks(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getNumCompletionChunks",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string) {
          return clang_getNumCompletionChunks((completion_string)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the number of chunks in the given code-completion string.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getNumCompletionChunks"; }
};

struct Entity_clang_getNumCompletionChunks
    : public Bind_clang_getNumCompletionChunks<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getNumCompletionChunks(EntityScope parent_h)
      : Bind_clang_getNumCompletionChunks<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getNumCompletionChunks

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionPriority
template <class Pybind11T>
struct Bind_clang_getCompletionPriority : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionPriority(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionPriority",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string) {
          return clang_getCompletionPriority((completion_string)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine the priority of this code completion.
 *
 * The priority of a code completion indicates how likely it is that this
 * particular completion is the completion that the user will select. The
 * priority is selected by various internal heuristics.
 *
 * \param completion_string The completion string to query.
 *
 * \returns The priority of this completion string. Smaller values indicate
 * higher-priority (more likely) completions.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionPriority"; }
};

struct Entity_clang_getCompletionPriority
    : public Bind_clang_getCompletionPriority<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionPriority(EntityScope parent_h)
      : Bind_clang_getCompletionPriority<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionPriority

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionAvailability
template <class Pybind11T>
struct Bind_clang_getCompletionAvailability : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionAvailability(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionAvailability",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string) {
          return clang_getCompletionAvailability((completion_string)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine the availability of the entity that this code-completion
 * string refers to.
 *
 * \param completion_string The completion string to query.
 *
 * \returns The availability of the completion string.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionAvailability"; }
};

struct Entity_clang_getCompletionAvailability
    : public Bind_clang_getCompletionAvailability<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionAvailability(EntityScope parent_h)
      : Bind_clang_getCompletionAvailability<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionAvailability

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionNumAnnotations
template <class Pybind11T>
struct Bind_clang_getCompletionNumAnnotations : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionNumAnnotations(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionNumAnnotations",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string) {
          return clang_getCompletionNumAnnotations((completion_string)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the number of annotations associated with the given
 * completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \returns the number of annotations associated with the given completion
 * string.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionNumAnnotations"; }
};

struct Entity_clang_getCompletionNumAnnotations
    : public Bind_clang_getCompletionNumAnnotations<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionNumAnnotations(EntityScope parent_h)
      : Bind_clang_getCompletionNumAnnotations<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionNumAnnotations

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionAnnotation
template <class Pybind11T>
struct Bind_clang_getCompletionAnnotation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionAnnotation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionAnnotation",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string,
            unsigned int annotation_number) {
          return clang_getCompletionAnnotation((completion_string)->ptr,
                                               annotation_number);
        },
        R"_pb11_weaver(/**
 * Retrieve the annotation associated with the given completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param annotation_number the 0-based index of the annotation of the
 * completion string.
 *
 * \returns annotation string associated with the completion at index
 * \c annotation_number, or a NULL string if that annotation is not available.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionAnnotation"; }
};

struct Entity_clang_getCompletionAnnotation
    : public Bind_clang_getCompletionAnnotation<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionAnnotation(EntityScope parent_h)
      : Bind_clang_getCompletionAnnotation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionAnnotation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionParent
template <class Pybind11T>
struct Bind_clang_getCompletionParent : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionParent(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionParent",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string,
            CXCursorKind *kind) {
          return clang_getCompletionParent((completion_string)->ptr, kind);
        },
        R"_pb11_weaver(/**
 * Retrieve the parent context of the given completion string.
 *
 * The parent context of a completion string is the semantic parent of
 * the declaration (if any) that the code completion represents. For example,
 * a code completion for an Objective-C method would have the method's class
 * or protocol as its context.
 *
 * \param completion_string The code completion string whose parent is
 * being queried.
 *
 * \param kind DEPRECATED: always set to CXCursor_NotImplemented if non-NULL.
 *
 * \returns The name of the completion parent, e.g., "NSObject" if
 * the completion string represents a method in the NSObject class.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionParent"; }
};

struct Entity_clang_getCompletionParent
    : public Bind_clang_getCompletionParent<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionParent(EntityScope parent_h)
      : Bind_clang_getCompletionParent<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionParent

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionBriefComment
template <class Pybind11T>
struct Bind_clang_getCompletionBriefComment : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionBriefComment(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionBriefComment",
        [=](pybind11_weaver::PointerWrapper<void *> *completion_string) {
          return clang_getCompletionBriefComment((completion_string)->ptr);
        },
        R"_pb11_weaver(/**
 * Retrieve the brief documentation comment attached to the declaration
 * that corresponds to the given completion string.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionBriefComment"; }
};

struct Entity_clang_getCompletionBriefComment
    : public Bind_clang_getCompletionBriefComment<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionBriefComment(EntityScope parent_h)
      : Bind_clang_getCompletionBriefComment<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionBriefComment

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCursorCompletionString
template <class Pybind11T>
struct Bind_clang_getCursorCompletionString : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCursorCompletionString(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCursorCompletionString",
        [=](CXCursor cursor) {
          return pybind11_weaver::WrapP<void *>(
                     clang_getCursorCompletionString(cursor))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve a completion string for an arbitrary declaration or macro
 * definition cursor.
 *
 * \param cursor The cursor to query.
 *
 * \returns A non-context-sensitive completion string for declaration and macro
 * definition cursors, or NULL for other kinds of cursors.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCursorCompletionString"; }
};

struct Entity_clang_getCursorCompletionString
    : public Bind_clang_getCursorCompletionString<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCursorCompletionString(EntityScope parent_h)
      : Bind_clang_getCursorCompletionString<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCursorCompletionString

#ifndef PB11_WEAVER_DISABLE_Entity_CXCodeCompleteResults
template <class Pybind11T>
struct Bind_CXCodeCompleteResults : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCodeCompleteResults(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * The number of code-completion results stored in the
   * \c Results array.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("Results", &CXCodeCompleteResults::Results,
                           R"_pb11_weaver(/**
   * The code-completion results.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("NumResults", &CXCodeCompleteResults::NumResults,
                           R"_pb11_weaver(/**
   * The number of code-completion results stored in the
   * \c Results array.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXCodeCompleteResults"; }
};

struct Entity_CXCodeCompleteResults
    : public Bind_CXCodeCompleteResults<
          std::decay_t<pybind11::class_<CXCodeCompleteResults>>> {

  explicit Entity_CXCodeCompleteResults(EntityScope parent_h)
      : Bind_CXCodeCompleteResults<
            std::decay_t<pybind11::class_<CXCodeCompleteResults>>>(parent_h),
        handle{parent_h, "CXCodeCompleteResults", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Contains the results of code-completion.
 *
 * This data structure contains the results of code completion, as
 * produced by \c clang_codeCompleteAt(). Its contents must be freed by
 * \c clang_disposeCodeCompleteResults.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXCodeCompleteResults> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCodeCompleteResults

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionNumFixIts
template <class Pybind11T>
struct Bind_clang_getCompletionNumFixIts : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionNumFixIts(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getCompletionNumFixIts",
        static_cast<unsigned int (*)(CXCodeCompleteResults *, unsigned int)>(
            &clang_getCompletionNumFixIts),
        R"_pb11_weaver(/**
 * Retrieve the number of fix-its for the given completion index.
 *
 * Calling this makes sense only if CXCodeComplete_IncludeCompletionsWithFixIts
 * option was set.
 *
 * \param results The structure keeping all completion results
 *
 * \param completion_index The index of the completion
 *
 * \return The number of fix-its which must be applied before the completion at
 * completion_index can be applied
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionNumFixIts"; }
};

struct Entity_clang_getCompletionNumFixIts
    : public Bind_clang_getCompletionNumFixIts<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionNumFixIts(EntityScope parent_h)
      : Bind_clang_getCompletionNumFixIts<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionNumFixIts

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getCompletionFixIt
template <class Pybind11T>
struct Bind_clang_getCompletionFixIt : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getCompletionFixIt(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getCompletionFixIt",
                 static_cast<CXString (*)(CXCodeCompleteResults *, unsigned int,
                                          unsigned int, CXSourceRange *)>(
                     &clang_getCompletionFixIt),
                 R"_pb11_weaver(/**
 * Fix-its that *must* be applied before inserting the text for the
 * corresponding completion.
 *
 * By default, clang_codeCompleteAt() only returns completions with empty
 * fix-its. Extra completions with non-empty fix-its should be explicitly
 * requested by setting CXCodeComplete_IncludeCompletionsWithFixIts.
 *
 * For the clients to be able to compute position of the cursor after applying
 * fix-its, the following conditions are guaranteed to hold for
 * replacement_range of the stored fix-its:
 *  - Ranges in the fix-its are guaranteed to never contain the completion
 *  point (or identifier under completion point, if any) inside them, except
 *  at the start or at the end of the range.
 *  - If a fix-it range starts or ends with completion point (or starts or
 *  ends after the identifier under completion point), it will contain at
 *  least one character. It allows to unambiguously recompute completion
 *  point after applying the fix-it.
 *
 * The intuition is that provided fix-its change code around the identifier we
 * complete, but are not allowed to touch the identifier itself or the
 * completion point. One example of completions with corrections are the ones
 * replacing '.' with '->' and vice versa:
 *
 * std::unique_ptr<std::vector<int>> vec_ptr;
 * In 'vec_ptr.^', one of the completions is 'push_back', it requires
 * replacing '.' with '->'.
 * In 'vec_ptr->^', one of the completions is 'release', it requires
 * replacing '->' with '.'.
 *
 * \param results The structure keeping all completion results
 *
 * \param completion_index The index of the completion
 *
 * \param fixit_index The index of the fix-it for the completion at
 * completion_index
 *
 * \param replacement_range The fix-it range that must be replaced before the
 * completion at completion_index can be applied
 *
 * \returns The fix-it string that must replace the code at replacement_range
 * before the completion at completion_index can be applied
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getCompletionFixIt"; }
};

struct Entity_clang_getCompletionFixIt
    : public Bind_clang_getCompletionFixIt<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getCompletionFixIt(EntityScope parent_h)
      : Bind_clang_getCompletionFixIt<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getCompletionFixIt

#ifndef PB11_WEAVER_DISABLE_Entity_CXCodeComplete_Flags
template <class Pybind11T>
struct Bind_CXCodeComplete_Flags : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCodeComplete_Flags(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCodeComplete_IncludeMacros",
                   CXCodeComplete_Flags::CXCodeComplete_IncludeMacros,
                   R"_pb11_weaver(/**
   * Whether to include macros within the set of code
   * completions returned.
   */)_pb11_weaver");
    pb11_obj.value("CXCodeComplete_IncludeCodePatterns",
                   CXCodeComplete_Flags::CXCodeComplete_IncludeCodePatterns,
                   R"_pb11_weaver(/**
   * Whether to include code patterns for language constructs
   * within the set of code completions, e.g., for loops.
   */)_pb11_weaver");
    pb11_obj.value("CXCodeComplete_IncludeBriefComments",
                   CXCodeComplete_Flags::CXCodeComplete_IncludeBriefComments,
                   R"_pb11_weaver(/**
   * Whether to include brief documentation within the set of code
   * completions returned.
   */)_pb11_weaver");
    pb11_obj.value("CXCodeComplete_SkipPreamble",
                   CXCodeComplete_Flags::CXCodeComplete_SkipPreamble,
                   R"_pb11_weaver(/**
   * Whether to speed up completion by omitting top- or namespace-level entities
   * defined in the preamble. There's no guarantee any particular entity is
   * omitted. This may be useful if the headers are indexed externally.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCodeComplete_IncludeCompletionsWithFixIts",
        CXCodeComplete_Flags::CXCodeComplete_IncludeCompletionsWithFixIts,
        R"_pb11_weaver(/**
   * Whether to include completions with small
   * fix-its, e.g. change '.' to '->' on member access, etc.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCodeComplete_Flags"; }
};

struct Entity_CXCodeComplete_Flags
    : public Bind_CXCodeComplete_Flags<
          std::decay_t<pybind11::enum_<CXCodeComplete_Flags>>> {

  explicit Entity_CXCodeComplete_Flags(EntityScope parent_h)
      : Bind_CXCodeComplete_Flags<
            std::decay_t<pybind11::enum_<CXCodeComplete_Flags>>>(parent_h),
        handle{parent_h, "CXCodeComplete_Flags", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Flags that can be passed to \c clang_codeCompleteAt() to
 * modify its behavior.
 *
 * The enumerators in this enumeration can be bitwise-OR'd together to
 * provide multiple options to \c clang_codeCompleteAt().
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCodeComplete_Flags> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCodeComplete_Flags

#ifndef PB11_WEAVER_DISABLE_Entity_CXCompletionContext
template <class Pybind11T> struct Bind_CXCompletionContext : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCompletionContext(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCompletionContext_Unexposed",
                   CXCompletionContext::CXCompletionContext_Unexposed,
                   R"_pb11_weaver(/**
   * The context for completions is unexposed, as only Clang results
   * should be included. (This is equivalent to having no context bits set.)
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_AnyType",
                   CXCompletionContext::CXCompletionContext_AnyType,
                   R"_pb11_weaver(/**
   * Completions for any possible type should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_AnyValue",
                   CXCompletionContext::CXCompletionContext_AnyValue,
                   R"_pb11_weaver(/**
   * Completions for any possible value (variables, function calls, etc.)
   * should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCObjectValue",
                   CXCompletionContext::CXCompletionContext_ObjCObjectValue,
                   R"_pb11_weaver(/**
   * Completions for values that resolve to an Objective-C object should
   * be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCSelectorValue",
                   CXCompletionContext::CXCompletionContext_ObjCSelectorValue,
                   R"_pb11_weaver(/**
   * Completions for values that resolve to an Objective-C selector
   * should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_CXXClassTypeValue",
                   CXCompletionContext::CXCompletionContext_CXXClassTypeValue,
                   R"_pb11_weaver(/**
   * Completions for values that resolve to a C++ class type should be
   * included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_DotMemberAccess",
                   CXCompletionContext::CXCompletionContext_DotMemberAccess,
                   R"_pb11_weaver(/**
   * Completions for fields of the member being accessed using the dot
   * operator should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ArrowMemberAccess",
                   CXCompletionContext::CXCompletionContext_ArrowMemberAccess,
                   R"_pb11_weaver(/**
   * Completions for fields of the member being accessed using the arrow
   * operator should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCPropertyAccess",
                   CXCompletionContext::CXCompletionContext_ObjCPropertyAccess,
                   R"_pb11_weaver(/**
   * Completions for properties of the Objective-C object being accessed
   * using the dot operator should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_EnumTag",
                   CXCompletionContext::CXCompletionContext_EnumTag,
                   R"_pb11_weaver(/**
   * Completions for enum tags should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_UnionTag",
                   CXCompletionContext::CXCompletionContext_UnionTag,
                   R"_pb11_weaver(/**
   * Completions for union tags should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_StructTag",
                   CXCompletionContext::CXCompletionContext_StructTag,
                   R"_pb11_weaver(/**
   * Completions for struct tags should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ClassTag",
                   CXCompletionContext::CXCompletionContext_ClassTag,
                   R"_pb11_weaver(/**
   * Completions for C++ class names should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_Namespace",
                   CXCompletionContext::CXCompletionContext_Namespace,
                   R"_pb11_weaver(/**
   * Completions for C++ namespaces and namespace aliases should be
   * included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_NestedNameSpecifier",
                   CXCompletionContext::CXCompletionContext_NestedNameSpecifier,
                   R"_pb11_weaver(/**
   * Completions for C++ nested name specifiers should be included in
   * the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCInterface",
                   CXCompletionContext::CXCompletionContext_ObjCInterface,
                   R"_pb11_weaver(/**
   * Completions for Objective-C interfaces (classes) should be included
   * in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCProtocol",
                   CXCompletionContext::CXCompletionContext_ObjCProtocol,
                   R"_pb11_weaver(/**
   * Completions for Objective-C protocols should be included in
   * the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCCategory",
                   CXCompletionContext::CXCompletionContext_ObjCCategory,
                   R"_pb11_weaver(/**
   * Completions for Objective-C categories should be included in
   * the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCInstanceMessage",
                   CXCompletionContext::CXCompletionContext_ObjCInstanceMessage,
                   R"_pb11_weaver(/**
   * Completions for Objective-C instance messages should be included
   * in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCClassMessage",
                   CXCompletionContext::CXCompletionContext_ObjCClassMessage,
                   R"_pb11_weaver(/**
   * Completions for Objective-C class messages should be included in
   * the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_ObjCSelectorName",
                   CXCompletionContext::CXCompletionContext_ObjCSelectorName,
                   R"_pb11_weaver(/**
   * Completions for Objective-C selector names should be included in
   * the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_MacroName",
                   CXCompletionContext::CXCompletionContext_MacroName,
                   R"_pb11_weaver(/**
   * Completions for preprocessor macro names should be included in
   * the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_NaturalLanguage",
                   CXCompletionContext::CXCompletionContext_NaturalLanguage,
                   R"_pb11_weaver(/**
   * Natural language completions should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_IncludedFile",
                   CXCompletionContext::CXCompletionContext_IncludedFile,
                   R"_pb11_weaver(/**
   * #include file completions should be included in the results.
   */)_pb11_weaver");
    pb11_obj.value("CXCompletionContext_Unknown",
                   CXCompletionContext::CXCompletionContext_Unknown,
                   R"_pb11_weaver(/**
   * The current context is unknown, so set all contexts.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCompletionContext"; }
};

struct Entity_CXCompletionContext
    : public Bind_CXCompletionContext<
          std::decay_t<pybind11::enum_<CXCompletionContext>>> {

  explicit Entity_CXCompletionContext(EntityScope parent_h)
      : Bind_CXCompletionContext<
            std::decay_t<pybind11::enum_<CXCompletionContext>>>(parent_h),
        handle{parent_h, "CXCompletionContext", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Bits that represent the context under which completion is occurring.
 *
 * The enumerators in this enumeration may be bitwise-OR'd together if multiple
 * contexts are occurring simultaneously.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCompletionContext> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCompletionContext

#ifndef PB11_WEAVER_DISABLE_Entity_clang_defaultCodeCompleteOptions
template <class Pybind11T>
struct Bind_clang_defaultCodeCompleteOptions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_defaultCodeCompleteOptions(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_defaultCodeCompleteOptions",
        static_cast<unsigned int (*)()>(&clang_defaultCodeCompleteOptions),
        R"_pb11_weaver(/**
 * Returns a default set of code-completion options that can be
 * passed to\c clang_codeCompleteAt().
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_defaultCodeCompleteOptions"; }
};

struct Entity_clang_defaultCodeCompleteOptions
    : public Bind_clang_defaultCodeCompleteOptions<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_defaultCodeCompleteOptions(EntityScope parent_h)
      : Bind_clang_defaultCodeCompleteOptions<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_defaultCodeCompleteOptions

#ifndef PB11_WEAVER_DISABLE_Entity_clang_codeCompleteAt
template <class Pybind11T>
struct Bind_clang_codeCompleteAt : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_codeCompleteAt(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_codeCompleteAt",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU,
            const char *complete_filename, unsigned int complete_line,
            unsigned int complete_column, CXUnsavedFile *unsaved_files,
            unsigned int num_unsaved_files, unsigned int options) {
          return clang_codeCompleteAt(
              (TU)->ptr, complete_filename, complete_line, complete_column,
              unsaved_files, num_unsaved_files, options);
        },
        R"_pb11_weaver(/**
 * Perform code completion at a given location in a translation unit.
 *
 * This function performs code completion at a particular file, line, and
 * column within source code, providing results that suggest potential
 * code snippets based on the context of the completion. The basic model
 * for code completion is that Clang will parse a complete source file,
 * performing syntax checking up to the location where code-completion has
 * been requested. At that point, a special code-completion token is passed
 * to the parser, which recognizes this token and determines, based on the
 * current location in the C/Objective-C/C++ grammar and the state of
 * semantic analysis, what completions to provide. These completions are
 * returned via a new \c CXCodeCompleteResults structure.
 *
 * Code completion itself is meant to be triggered by the client when the
 * user types punctuation characters or whitespace, at which point the
 * code-completion location will coincide with the cursor. For example, if \c p
 * is a pointer, code-completion might be triggered after the "-" and then
 * after the ">" in \c p->. When the code-completion location is after the ">",
 * the completion results will provide, e.g., the members of the struct that
 * "p" points to. The client is responsible for placing the cursor at the
 * beginning of the token currently being typed, then filtering the results
 * based on the contents of the token. For example, when code-completing for
 * the expression \c p->get, the client should provide the location just after
 * the ">" (e.g., pointing at the "g") to this code-completion hook. Then, the
 * client can filter the results based on the current token text ("get"), only
 * showing those results that start with "get". The intent of this interface
 * is to separate the relatively high-latency acquisition of code-completion
 * results from the filtering of results on a per-character basis, which must
 * have a lower latency.
 *
 * \param TU The translation unit in which code-completion should
 * occur. The source files for this translation unit need not be
 * completely up-to-date (and the contents of those source files may
 * be overridden via \p unsaved_files). Cursors referring into the
 * translation unit may be invalidated by this invocation.
 *
 * \param complete_filename The name of the source file where code
 * completion should be performed. This filename may be any file
 * included in the translation unit.
 *
 * \param complete_line The line at which code-completion should occur.
 *
 * \param complete_column The column at which code-completion should occur.
 * Note that the column should point just after the syntactic construct that
 * initiated code completion, and not in the middle of a lexical token.
 *
 * \param unsaved_files the Files that have not yet been saved to disk
 * but may be required for parsing or code completion, including the
 * contents of those files.  The contents and name of these files (as
 * specified by CXUnsavedFile) are copied when necessary, so the
 * client only needs to guarantee their validity until the call to
 * this function returns.
 *
 * \param num_unsaved_files The number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param options Extra options that control the behavior of code
 * completion, expressed as a bitwise OR of the enumerators of the
 * CXCodeComplete_Flags enumeration. The
 * \c clang_defaultCodeCompleteOptions() function returns a default set
 * of code-completion options.
 *
 * \returns If successful, a new \c CXCodeCompleteResults structure
 * containing code-completion results, which should eventually be
 * freed with \c clang_disposeCodeCompleteResults(). If code
 * completion fails, returns NULL.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_codeCompleteAt"; }
};

struct Entity_clang_codeCompleteAt
    : public Bind_clang_codeCompleteAt<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_codeCompleteAt(EntityScope parent_h)
      : Bind_clang_codeCompleteAt<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_codeCompleteAt

#ifndef PB11_WEAVER_DISABLE_Entity_clang_sortCodeCompletionResults
template <class Pybind11T>
struct Bind_clang_sortCodeCompletionResults : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_sortCodeCompletionResults(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_sortCodeCompletionResults",
                 static_cast<void (*)(CXCompletionResult *, unsigned int)>(
                     &clang_sortCodeCompletionResults),
                 R"_pb11_weaver(/**
 * Sort the code-completion results in case-insensitive alphabetical
 * order.
 *
 * \param Results The set of results to sort.
 * \param NumResults The number of results in \p Results.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_sortCodeCompletionResults"; }
};

struct Entity_clang_sortCodeCompletionResults
    : public Bind_clang_sortCodeCompletionResults<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_sortCodeCompletionResults(EntityScope parent_h)
      : Bind_clang_sortCodeCompletionResults<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_sortCodeCompletionResults

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeCodeCompleteResults
template <class Pybind11T>
struct Bind_clang_disposeCodeCompleteResults : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeCodeCompleteResults(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_disposeCodeCompleteResults",
                 static_cast<void (*)(CXCodeCompleteResults *)>(
                     &clang_disposeCodeCompleteResults),
                 R"_pb11_weaver(/**
 * Free the given set of code-completion results.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeCodeCompleteResults"; }
};

struct Entity_clang_disposeCodeCompleteResults
    : public Bind_clang_disposeCodeCompleteResults<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeCodeCompleteResults(EntityScope parent_h)
      : Bind_clang_disposeCodeCompleteResults<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeCodeCompleteResults

#ifndef PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetNumDiagnostics
template <class Pybind11T>
struct Bind_clang_codeCompleteGetNumDiagnostics : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_codeCompleteGetNumDiagnostics(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_codeCompleteGetNumDiagnostics",
                 static_cast<unsigned int (*)(CXCodeCompleteResults *)>(
                     &clang_codeCompleteGetNumDiagnostics),
                 R"_pb11_weaver(/**
 * Determine the number of diagnostics produced prior to the
 * location where code completion was performed.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_codeCompleteGetNumDiagnostics"; }
};

struct Entity_clang_codeCompleteGetNumDiagnostics
    : public Bind_clang_codeCompleteGetNumDiagnostics<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_codeCompleteGetNumDiagnostics(EntityScope parent_h)
      : Bind_clang_codeCompleteGetNumDiagnostics<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetNumDiagnostics

#ifndef PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetDiagnostic
template <class Pybind11T>
struct Bind_clang_codeCompleteGetDiagnostic : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_codeCompleteGetDiagnostic(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_codeCompleteGetDiagnostic",
        [=](CXCodeCompleteResults *Results, unsigned int Index) {
          return pybind11_weaver::WrapP<void *>(
                     clang_codeCompleteGetDiagnostic(Results, Index))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve a diagnostic associated with the given code completion.
 *
 * \param Results the code completion results to query.
 * \param Index the zero-based diagnostic number to retrieve.
 *
 * \returns the requested diagnostic. This diagnostic must be freed
 * via a call to \c clang_disposeDiagnostic().
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_codeCompleteGetDiagnostic"; }
};

struct Entity_clang_codeCompleteGetDiagnostic
    : public Bind_clang_codeCompleteGetDiagnostic<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_codeCompleteGetDiagnostic(EntityScope parent_h)
      : Bind_clang_codeCompleteGetDiagnostic<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetDiagnostic

#ifndef PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetContexts
template <class Pybind11T>
struct Bind_clang_codeCompleteGetContexts : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_codeCompleteGetContexts(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_codeCompleteGetContexts",
                 static_cast<unsigned long long (*)(CXCodeCompleteResults *)>(
                     &clang_codeCompleteGetContexts),
                 R"_pb11_weaver(/**
 * Determines what completions are appropriate for the context
 * the given code completion.
 *
 * \param Results the code completion results to query
 *
 * \returns the kinds of completions that are appropriate for use
 * along with the given code completion results.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_codeCompleteGetContexts"; }
};

struct Entity_clang_codeCompleteGetContexts
    : public Bind_clang_codeCompleteGetContexts<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_codeCompleteGetContexts(EntityScope parent_h)
      : Bind_clang_codeCompleteGetContexts<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetContexts

#ifndef PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetContainerKind
template <class Pybind11T>
struct Bind_clang_codeCompleteGetContainerKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_codeCompleteGetContainerKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_codeCompleteGetContainerKind",
        static_cast<CXCursorKind (*)(CXCodeCompleteResults *, unsigned int *)>(
            &clang_codeCompleteGetContainerKind),
        R"_pb11_weaver(/**
 * Returns the cursor kind for the container for the current code
 * completion context. The container is only guaranteed to be set for
 * contexts where a container exists (i.e. member accesses or Objective-C
 * message sends); if there is not a container, this function will return
 * CXCursor_InvalidCode.
 *
 * \param Results the code completion results to query
 *
 * \param IsIncomplete on return, this value will be false if Clang has complete
 * information about the container. If Clang does not have complete
 * information, this value will be true.
 *
 * \returns the container kind, or CXCursor_InvalidCode if there is not a
 * container
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_codeCompleteGetContainerKind"; }
};

struct Entity_clang_codeCompleteGetContainerKind
    : public Bind_clang_codeCompleteGetContainerKind<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_codeCompleteGetContainerKind(EntityScope parent_h)
      : Bind_clang_codeCompleteGetContainerKind<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetContainerKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetContainerUSR
template <class Pybind11T>
struct Bind_clang_codeCompleteGetContainerUSR : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_codeCompleteGetContainerUSR(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_codeCompleteGetContainerUSR",
                 static_cast<CXString (*)(CXCodeCompleteResults *)>(
                     &clang_codeCompleteGetContainerUSR),
                 R"_pb11_weaver(/**
 * Returns the USR for the container for the current code completion
 * context. If there is not a container for the current context, this
 * function will return the empty string.
 *
 * \param Results the code completion results to query
 *
 * \returns the USR for the container
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_codeCompleteGetContainerUSR"; }
};

struct Entity_clang_codeCompleteGetContainerUSR
    : public Bind_clang_codeCompleteGetContainerUSR<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_codeCompleteGetContainerUSR(EntityScope parent_h)
      : Bind_clang_codeCompleteGetContainerUSR<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetContainerUSR

#ifndef PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetObjCSelector
template <class Pybind11T>
struct Bind_clang_codeCompleteGetObjCSelector : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_codeCompleteGetObjCSelector(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_codeCompleteGetObjCSelector",
                 static_cast<CXString (*)(CXCodeCompleteResults *)>(
                     &clang_codeCompleteGetObjCSelector),
                 R"_pb11_weaver(/**
 * Returns the currently-entered selector for an Objective-C message
 * send, formatted like "initWithFoo:bar:". Only guaranteed to return a
 * non-empty string for CXCompletionContext_ObjCInstanceMessage and
 * CXCompletionContext_ObjCClassMessage.
 *
 * \param Results the code completion results to query
 *
 * \returns the selector (or partial selector) that has been entered thus far
 * for an Objective-C message send.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_codeCompleteGetObjCSelector"; }
};

struct Entity_clang_codeCompleteGetObjCSelector
    : public Bind_clang_codeCompleteGetObjCSelector<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_codeCompleteGetObjCSelector(EntityScope parent_h)
      : Bind_clang_codeCompleteGetObjCSelector<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_codeCompleteGetObjCSelector

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getClangVersion
template <class Pybind11T>
struct Bind_clang_getClangVersion : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getClangVersion(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_getClangVersion",
                 static_cast<CXString (*)()>(&clang_getClangVersion),
                 R"_pb11_weaver(/**
 * Return a version string, suitable for showing to a user, but not
 *        intended to be parsed (the format is not guaranteed to be stable).
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getClangVersion"; }
};

struct Entity_clang_getClangVersion
    : public Bind_clang_getClangVersion<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getClangVersion(EntityScope parent_h)
      : Bind_clang_getClangVersion<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getClangVersion

#ifndef PB11_WEAVER_DISABLE_Entity_clang_toggleCrashRecovery
template <class Pybind11T>
struct Bind_clang_toggleCrashRecovery : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_toggleCrashRecovery(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_toggleCrashRecovery",
        static_cast<void (*)(unsigned int)>(&clang_toggleCrashRecovery),
        R"_pb11_weaver(/**
 * Enable/disable crash recovery.
 *
 * \param isEnabled Flag to indicate if crash recovery is enabled.  A non-zero
 *        value enables crash recovery, while 0 disables it.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_toggleCrashRecovery"; }
};

struct Entity_clang_toggleCrashRecovery
    : public Bind_clang_toggleCrashRecovery<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_toggleCrashRecovery(EntityScope parent_h)
      : Bind_clang_toggleCrashRecovery<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_toggleCrashRecovery

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getInclusions
template <class Pybind11T> struct Bind_clang_getInclusions : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getInclusions(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getInclusions",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu,
            std::function<void(pybind11_weaver::PointerWrapper<void *> *,
                               CXSourceLocation *, unsigned int,
                               pybind11_weaver::PointerWrapper<void *> *)>
                visitor,
            pybind11_weaver::PointerWrapper<void *> *client_data) {
          return clang_getInclusions(
              (tu)->ptr,
              pybind11_weaver::FnPointerWrapper<
                  void, pybind11_weaver::PointerWrapper<void *> *,
                  CXSourceLocation *, unsigned int,
                  pybind11_weaver::PointerWrapper<void *>
                      *>::GetCptr<void, void *, CXSourceLocation *,
                                  unsigned int, void *>::
                  Run((visitor), pybind11_weaver::Guardian(),
                      [](void *arg0_0, CXSourceLocation *arg0_1,
                         unsigned int arg0_2, void *arg0_3) {
                        auto &to_call = pybind11_weaver::FnPointerWrapper<
                            void, pybind11_weaver::PointerWrapper<void *> *,
                            CXSourceLocation *, unsigned int,
                            pybind11_weaver::PointerWrapper<void *>
                                *>::FnProxy(3);
                        return to_call(
                            pybind11_weaver::WrapP<void *>(arg0_0).get(),
                            arg0_1, arg0_2,
                            pybind11_weaver::WrapP<void *>(arg0_3).get());
                      },
                      3),
              (client_data)->ptr);
        },
        R"_pb11_weaver(/**
 * Visit the set of preprocessor inclusions in a translation unit.
 *   The visitor function is called with the provided data for every included
 *   file.  This does not include headers included by the PCH file (unless one
 *   is inspecting the inclusions in the PCH file itself).
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getInclusions"; }
};

struct Entity_clang_getInclusions
    : public Bind_clang_getInclusions<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getInclusions(EntityScope parent_h)
      : Bind_clang_getInclusions<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getInclusions

#ifndef PB11_WEAVER_DISABLE_Entity_CXEvalResultKind
template <class Pybind11T> struct Bind_CXEvalResultKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXEvalResultKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXEval_Int", CXEvalResultKind::CXEval_Int);
    pb11_obj.value("CXEval_Float", CXEvalResultKind::CXEval_Float);
    pb11_obj.value("CXEval_ObjCStrLiteral",
                   CXEvalResultKind::CXEval_ObjCStrLiteral);
    pb11_obj.value("CXEval_StrLiteral", CXEvalResultKind::CXEval_StrLiteral);
    pb11_obj.value("CXEval_CFStr", CXEvalResultKind::CXEval_CFStr);
    pb11_obj.value("CXEval_Other", CXEvalResultKind::CXEval_Other);
    pb11_obj.value("CXEval_UnExposed", CXEvalResultKind::CXEval_UnExposed);
  }

  static const char *Key() { return "CXEvalResultKind"; }
};

struct Entity_CXEvalResultKind
    : public Bind_CXEvalResultKind<
          std::decay_t<pybind11::enum_<CXEvalResultKind>>> {

  explicit Entity_CXEvalResultKind(EntityScope parent_h)
      : Bind_CXEvalResultKind<std::decay_t<pybind11::enum_<CXEvalResultKind>>>(
            parent_h),
        handle{parent_h, "CXEvalResultKind", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXEvalResultKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXEvalResultKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_Evaluate
template <class Pybind11T>
struct Bind_clang_Cursor_Evaluate : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_Evaluate(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_Evaluate",
        [=](CXCursor C) {
          return pybind11_weaver::WrapP<void *>(clang_Cursor_Evaluate(C))
              .release();
        },
        R"_pb11_weaver(/**
 * If cursor is a statement declaration tries to evaluate the
 * statement and if its variable, tries to evaluate its initializer,
 * into its corresponding type.
 * If it's an expression, tries to evaluate the expression.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_Evaluate"; }
};

struct Entity_clang_Cursor_Evaluate
    : public Bind_clang_Cursor_Evaluate<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_Evaluate(EntityScope parent_h)
      : Bind_clang_Cursor_Evaluate<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_Evaluate

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getKind
template <class Pybind11T>
struct Bind_clang_EvalResult_getKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EvalResult_getKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EvalResult_getKind",
        [=](pybind11_weaver::PointerWrapper<void *> *E) {
          return clang_EvalResult_getKind((E)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the kind of the evaluated result.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EvalResult_getKind"; }
};

struct Entity_clang_EvalResult_getKind
    : public Bind_clang_EvalResult_getKind<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EvalResult_getKind(EntityScope parent_h)
      : Bind_clang_EvalResult_getKind<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsInt
template <class Pybind11T>
struct Bind_clang_EvalResult_getAsInt : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EvalResult_getAsInt(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EvalResult_getAsInt",
        [=](pybind11_weaver::PointerWrapper<void *> *E) {
          return clang_EvalResult_getAsInt((E)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the evaluation result as integer if the
 * kind is Int.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EvalResult_getAsInt"; }
};

struct Entity_clang_EvalResult_getAsInt
    : public Bind_clang_EvalResult_getAsInt<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EvalResult_getAsInt(EntityScope parent_h)
      : Bind_clang_EvalResult_getAsInt<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsInt

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsLongLong
template <class Pybind11T>
struct Bind_clang_EvalResult_getAsLongLong : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EvalResult_getAsLongLong(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EvalResult_getAsLongLong",
        [=](pybind11_weaver::PointerWrapper<void *> *E) {
          return clang_EvalResult_getAsLongLong((E)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the evaluation result as a long long integer if the
 * kind is Int. This prevents overflows that may happen if the result is
 * returned with clang_EvalResult_getAsInt.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EvalResult_getAsLongLong"; }
};

struct Entity_clang_EvalResult_getAsLongLong
    : public Bind_clang_EvalResult_getAsLongLong<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EvalResult_getAsLongLong(EntityScope parent_h)
      : Bind_clang_EvalResult_getAsLongLong<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsLongLong

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EvalResult_isUnsignedInt
template <class Pybind11T>
struct Bind_clang_EvalResult_isUnsignedInt : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EvalResult_isUnsignedInt(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EvalResult_isUnsignedInt",
        [=](pybind11_weaver::PointerWrapper<void *> *E) {
          return clang_EvalResult_isUnsignedInt((E)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns a non-zero value if the kind is Int and the evaluation
 * result resulted in an unsigned integer.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EvalResult_isUnsignedInt"; }
};

struct Entity_clang_EvalResult_isUnsignedInt
    : public Bind_clang_EvalResult_isUnsignedInt<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EvalResult_isUnsignedInt(EntityScope parent_h)
      : Bind_clang_EvalResult_isUnsignedInt<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EvalResult_isUnsignedInt

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsUnsigned
template <class Pybind11T>
struct Bind_clang_EvalResult_getAsUnsigned : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EvalResult_getAsUnsigned(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EvalResult_getAsUnsigned",
        [=](pybind11_weaver::PointerWrapper<void *> *E) {
          return clang_EvalResult_getAsUnsigned((E)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the evaluation result as an unsigned integer if
 * the kind is Int and clang_EvalResult_isUnsignedInt is non-zero.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EvalResult_getAsUnsigned"; }
};

struct Entity_clang_EvalResult_getAsUnsigned
    : public Bind_clang_EvalResult_getAsUnsigned<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EvalResult_getAsUnsigned(EntityScope parent_h)
      : Bind_clang_EvalResult_getAsUnsigned<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsUnsigned

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsDouble
template <class Pybind11T>
struct Bind_clang_EvalResult_getAsDouble : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EvalResult_getAsDouble(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EvalResult_getAsDouble",
        [=](pybind11_weaver::PointerWrapper<void *> *E) {
          return clang_EvalResult_getAsDouble((E)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the evaluation result as double if the
 * kind is double.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EvalResult_getAsDouble"; }
};

struct Entity_clang_EvalResult_getAsDouble
    : public Bind_clang_EvalResult_getAsDouble<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EvalResult_getAsDouble(EntityScope parent_h)
      : Bind_clang_EvalResult_getAsDouble<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsDouble

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsStr
template <class Pybind11T>
struct Bind_clang_EvalResult_getAsStr : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EvalResult_getAsStr(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EvalResult_getAsStr",
        [=](pybind11_weaver::PointerWrapper<void *> *E) {
          return clang_EvalResult_getAsStr((E)->ptr);
        },
        R"_pb11_weaver(/**
 * Returns the evaluation result as a constant string if the
 * kind is other than Int or float. User must not free this pointer,
 * instead call clang_EvalResult_dispose on the CXEvalResult returned
 * by clang_Cursor_Evaluate.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EvalResult_getAsStr"; }
};

struct Entity_clang_EvalResult_getAsStr
    : public Bind_clang_EvalResult_getAsStr<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EvalResult_getAsStr(EntityScope parent_h)
      : Bind_clang_EvalResult_getAsStr<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EvalResult_getAsStr

#ifndef PB11_WEAVER_DISABLE_Entity_clang_EvalResult_dispose
template <class Pybind11T>
struct Bind_clang_EvalResult_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_EvalResult_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_EvalResult_dispose",
        [=](pybind11_weaver::PointerWrapper<void *> *E) {
          return clang_EvalResult_dispose((E)->ptr);
        },
        R"_pb11_weaver(/**
 * Disposes the created Eval memory.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_EvalResult_dispose"; }
};

struct Entity_clang_EvalResult_dispose
    : public Bind_clang_EvalResult_dispose<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_EvalResult_dispose(EntityScope parent_h)
      : Bind_clang_EvalResult_dispose<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_EvalResult_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getRemappings
template <class Pybind11T> struct Bind_clang_getRemappings : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getRemappings(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getRemappings",
        [=](const char *path) {
          return pybind11_weaver::WrapP<void *>(clang_getRemappings(path))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve a remapping.
 *
 * \param path the path that contains metadata about remappings.
 *
 * \returns the requested remapping. This remapping must be freed
 * via a call to \c clang_remap_dispose(). Can return NULL if an error occurred.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getRemappings"; }
};

struct Entity_clang_getRemappings
    : public Bind_clang_getRemappings<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getRemappings(EntityScope parent_h)
      : Bind_clang_getRemappings<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getRemappings

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getRemappingsFromFileList
template <class Pybind11T>
struct Bind_clang_getRemappingsFromFileList : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getRemappingsFromFileList(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getRemappingsFromFileList",
        [=](pybind11_weaver::PointerWrapper<const char **> *filePaths,
            unsigned int numFiles) {
          return pybind11_weaver::WrapP<void *>(clang_getRemappingsFromFileList(
                                                    (filePaths)->ptr, numFiles))
              .release();
        },
        R"_pb11_weaver(/**
 * Retrieve a remapping.
 *
 * \param filePaths pointer to an array of file paths containing remapping info.
 *
 * \param numFiles number of file paths.
 *
 * \returns the requested remapping. This remapping must be freed
 * via a call to \c clang_remap_dispose(). Can return NULL if an error occurred.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getRemappingsFromFileList"; }
};

struct Entity_clang_getRemappingsFromFileList
    : public Bind_clang_getRemappingsFromFileList<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getRemappingsFromFileList(EntityScope parent_h)
      : Bind_clang_getRemappingsFromFileList<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getRemappingsFromFileList

#ifndef PB11_WEAVER_DISABLE_Entity_clang_remap_getNumFiles
template <class Pybind11T>
struct Bind_clang_remap_getNumFiles : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_remap_getNumFiles(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_remap_getNumFiles",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_remap_getNumFiles((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Determine the number of remappings.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_remap_getNumFiles"; }
};

struct Entity_clang_remap_getNumFiles
    : public Bind_clang_remap_getNumFiles<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_remap_getNumFiles(EntityScope parent_h)
      : Bind_clang_remap_getNumFiles<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_remap_getNumFiles

#ifndef PB11_WEAVER_DISABLE_Entity_clang_remap_getFilenames
template <class Pybind11T>
struct Bind_clang_remap_getFilenames : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_remap_getFilenames(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_remap_getFilenames",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0, unsigned int index,
            CXString *original, CXString *transformed) {
          return clang_remap_getFilenames((arg0)->ptr, index, original,
                                          transformed);
        },
        R"_pb11_weaver(/**
 * Get the original and the associated filename from the remapping.
 *
 * \param original If non-NULL, will be set to the original filename.
 *
 * \param transformed If non-NULL, will be set to the filename that the original
 * is associated with.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_remap_getFilenames"; }
};

struct Entity_clang_remap_getFilenames
    : public Bind_clang_remap_getFilenames<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_remap_getFilenames(EntityScope parent_h)
      : Bind_clang_remap_getFilenames<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_remap_getFilenames

#ifndef PB11_WEAVER_DISABLE_Entity_clang_remap_dispose
template <class Pybind11T> struct Bind_clang_remap_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_remap_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_remap_dispose",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_remap_dispose((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Dispose the remapping.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_remap_dispose"; }
};

struct Entity_clang_remap_dispose
    : public Bind_clang_remap_dispose<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_remap_dispose(EntityScope parent_h)
      : Bind_clang_remap_dispose<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_remap_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_CXVisitorResult
template <class Pybind11T> struct Bind_CXVisitorResult : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXVisitorResult(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXVisit_Break", CXVisitorResult::CXVisit_Break);
    pb11_obj.value("CXVisit_Continue", CXVisitorResult::CXVisit_Continue);
  }

  static const char *Key() { return "CXVisitorResult"; }
};

struct Entity_CXVisitorResult
    : public Bind_CXVisitorResult<
          std::decay_t<pybind11::enum_<CXVisitorResult>>> {

  explicit Entity_CXVisitorResult(EntityScope parent_h)
      : Bind_CXVisitorResult<std::decay_t<pybind11::enum_<CXVisitorResult>>>(
            parent_h),
        handle{
            parent_h, "CXVisitorResult", pybind11::arithmetic(),
            R"_pb11_weaver(/** \defgroup CINDEX_HIGH Higher level API functions
 *
 * @{
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXVisitorResult> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXVisitorResult

#ifndef PB11_WEAVER_DISABLE_Entity_CXCursorAndRangeVisitor
template <class Pybind11T>
struct Bind_CXCursorAndRangeVisitor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCursorAndRangeVisitor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXCursorAndRangeVisitor"; }
};

struct Entity_CXCursorAndRangeVisitor
    : public Bind_CXCursorAndRangeVisitor<
          std::decay_t<pybind11::class_<CXCursorAndRangeVisitor>>> {

  explicit Entity_CXCursorAndRangeVisitor(EntityScope parent_h)
      : Bind_CXCursorAndRangeVisitor<
            std::decay_t<pybind11::class_<CXCursorAndRangeVisitor>>>(parent_h),
        handle{parent_h, "CXCursorAndRangeVisitor", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXCursorAndRangeVisitor> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCursorAndRangeVisitor

#ifndef PB11_WEAVER_DISABLE_Entity_CXResult
template <class Pybind11T> struct Bind_CXResult : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXResult(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXResult_Success", CXResult::CXResult_Success,
                   R"_pb11_weaver(/**
   * Function returned successfully.
   */)_pb11_weaver");
    pb11_obj.value("CXResult_Invalid", CXResult::CXResult_Invalid,
                   R"_pb11_weaver(/**
   * One of the parameters was invalid for the function.
   */)_pb11_weaver");
    pb11_obj.value("CXResult_VisitBreak", CXResult::CXResult_VisitBreak,
                   R"_pb11_weaver(/**
   * The function was terminated by a callback (e.g. it returned
   * CXVisit_Break)
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXResult"; }
};

struct Entity_CXResult
    : public Bind_CXResult<std::decay_t<pybind11::enum_<CXResult>>> {

  explicit Entity_CXResult(EntityScope parent_h)
      : Bind_CXResult<std::decay_t<pybind11::enum_<CXResult>>>(parent_h),
        handle{parent_h, "CXResult", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXResult> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXResult

#ifndef PB11_WEAVER_DISABLE_Entity_clang_findReferencesInFile
template <class Pybind11T>
struct Bind_clang_findReferencesInFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_findReferencesInFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_findReferencesInFile",
        [=](CXCursor cursor, pybind11_weaver::PointerWrapper<void *> *file,
            CXCursorAndRangeVisitor visitor) {
          return clang_findReferencesInFile(cursor, (file)->ptr, visitor);
        },
        R"_pb11_weaver(/**
 * Find references of a declaration in a specific file.
 *
 * \param cursor pointing to a declaration or a reference of one.
 *
 * \param file to search for references.
 *
 * \param visitor callback that will receive pairs of CXCursor/CXSourceRange for
 * each reference found.
 * The CXSourceRange will point inside the file; if the reference is inside
 * a macro (and not a macro argument) the CXSourceRange will be invalid.
 *
 * \returns one of the CXResult enumerators.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_findReferencesInFile"; }
};

struct Entity_clang_findReferencesInFile
    : public Bind_clang_findReferencesInFile<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_findReferencesInFile(EntityScope parent_h)
      : Bind_clang_findReferencesInFile<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_findReferencesInFile

#ifndef PB11_WEAVER_DISABLE_Entity_clang_findIncludesInFile
template <class Pybind11T>
struct Bind_clang_findIncludesInFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_findIncludesInFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_findIncludesInFile",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU,
            pybind11_weaver::PointerWrapper<void *> *file,
            CXCursorAndRangeVisitor visitor) {
          return clang_findIncludesInFile((TU)->ptr, (file)->ptr, visitor);
        },
        R"_pb11_weaver(/**
 * Find #import/#include directives in a specific file.
 *
 * \param TU translation unit containing the file to query.
 *
 * \param file to search for #import/#include directives.
 *
 * \param visitor callback that will receive pairs of CXCursor/CXSourceRange for
 * each directive found.
 *
 * \returns one of the CXResult enumerators.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_findIncludesInFile"; }
};

struct Entity_clang_findIncludesInFile
    : public Bind_clang_findIncludesInFile<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_findIncludesInFile(EntityScope parent_h)
      : Bind_clang_findIncludesInFile<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_findIncludesInFile

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxLoc
template <class Pybind11T> struct Bind_CXIdxLoc : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxLoc(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("int_data", &CXIdxLoc::int_data);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxLoc"; }
};

struct Entity_CXIdxLoc
    : public Bind_CXIdxLoc<std::decay_t<pybind11::class_<CXIdxLoc>>> {

  explicit Entity_CXIdxLoc(EntityScope parent_h)
      : Bind_CXIdxLoc<std::decay_t<pybind11::class_<CXIdxLoc>>>(parent_h),
        handle{parent_h, "CXIdxLoc", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Source location passed to index callbacks.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxLoc> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxLoc

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxIncludedFileInfo
template <class Pybind11T>
struct Bind_CXIdxIncludedFileInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxIncludedFileInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * Non-zero if the directive was automatically turned into a module
   * import.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("hashLoc", &CXIdxIncludedFileInfo::hashLoc,
                           R"_pb11_weaver(/**
   * Location of '#' in the \#include/\#import directive.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("filename", &CXIdxIncludedFileInfo::filename,
                           R"_pb11_weaver(/**
   * Filename as written in the \#include/\#import directive.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("isImport", &CXIdxIncludedFileInfo::isImport);
    pb11_obj.def_readwrite("isAngled", &CXIdxIncludedFileInfo::isAngled);
    pb11_obj.def_readwrite("isModuleImport",
                           &CXIdxIncludedFileInfo::isModuleImport,
                           R"_pb11_weaver(/**
   * Non-zero if the directive was automatically turned into a module
   * import.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxIncludedFileInfo"; }
};

struct Entity_CXIdxIncludedFileInfo
    : public Bind_CXIdxIncludedFileInfo<
          std::decay_t<pybind11::class_<CXIdxIncludedFileInfo>>> {

  explicit Entity_CXIdxIncludedFileInfo(EntityScope parent_h)
      : Bind_CXIdxIncludedFileInfo<
            std::decay_t<pybind11::class_<CXIdxIncludedFileInfo>>>(parent_h),
        handle{parent_h, "CXIdxIncludedFileInfo", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Data for ppIncludedFile callback.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxIncludedFileInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxIncludedFileInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxImportedASTFileInfo
template <class Pybind11T>
struct Bind_CXIdxImportedASTFileInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxImportedASTFileInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * Non-zero if an inclusion directive was automatically turned into
   * a module import. Applicable only for modules.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("loc", &CXIdxImportedASTFileInfo::loc,
                           R"_pb11_weaver(/**
   * Location where the file is imported. Applicable only for modules.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("isImplicit", &CXIdxImportedASTFileInfo::isImplicit,
                           R"_pb11_weaver(/**
   * Non-zero if an inclusion directive was automatically turned into
   * a module import. Applicable only for modules.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxImportedASTFileInfo"; }
};

struct Entity_CXIdxImportedASTFileInfo
    : public Bind_CXIdxImportedASTFileInfo<
          std::decay_t<pybind11::class_<CXIdxImportedASTFileInfo>>> {

  explicit Entity_CXIdxImportedASTFileInfo(EntityScope parent_h)
      : Bind_CXIdxImportedASTFileInfo<
            std::decay_t<pybind11::class_<CXIdxImportedASTFileInfo>>>(parent_h),
        handle{parent_h, "CXIdxImportedASTFileInfo", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Data for IndexerCallbacks#importedASTFile.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxImportedASTFileInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxImportedASTFileInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxEntityKind
template <class Pybind11T> struct Bind_CXIdxEntityKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxEntityKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXIdxEntity_Unexposed",
                   CXIdxEntityKind::CXIdxEntity_Unexposed);
    pb11_obj.value("CXIdxEntity_Typedef", CXIdxEntityKind::CXIdxEntity_Typedef);
    pb11_obj.value("CXIdxEntity_Function",
                   CXIdxEntityKind::CXIdxEntity_Function);
    pb11_obj.value("CXIdxEntity_Variable",
                   CXIdxEntityKind::CXIdxEntity_Variable);
    pb11_obj.value("CXIdxEntity_Field", CXIdxEntityKind::CXIdxEntity_Field);
    pb11_obj.value("CXIdxEntity_EnumConstant",
                   CXIdxEntityKind::CXIdxEntity_EnumConstant);
    pb11_obj.value("CXIdxEntity_ObjCClass",
                   CXIdxEntityKind::CXIdxEntity_ObjCClass);
    pb11_obj.value("CXIdxEntity_ObjCProtocol",
                   CXIdxEntityKind::CXIdxEntity_ObjCProtocol);
    pb11_obj.value("CXIdxEntity_ObjCCategory",
                   CXIdxEntityKind::CXIdxEntity_ObjCCategory);
    pb11_obj.value("CXIdxEntity_ObjCInstanceMethod",
                   CXIdxEntityKind::CXIdxEntity_ObjCInstanceMethod);
    pb11_obj.value("CXIdxEntity_ObjCClassMethod",
                   CXIdxEntityKind::CXIdxEntity_ObjCClassMethod);
    pb11_obj.value("CXIdxEntity_ObjCProperty",
                   CXIdxEntityKind::CXIdxEntity_ObjCProperty);
    pb11_obj.value("CXIdxEntity_ObjCIvar",
                   CXIdxEntityKind::CXIdxEntity_ObjCIvar);
    pb11_obj.value("CXIdxEntity_Enum", CXIdxEntityKind::CXIdxEntity_Enum);
    pb11_obj.value("CXIdxEntity_Struct", CXIdxEntityKind::CXIdxEntity_Struct);
    pb11_obj.value("CXIdxEntity_Union", CXIdxEntityKind::CXIdxEntity_Union);
    pb11_obj.value("CXIdxEntity_CXXClass",
                   CXIdxEntityKind::CXIdxEntity_CXXClass);
    pb11_obj.value("CXIdxEntity_CXXNamespace",
                   CXIdxEntityKind::CXIdxEntity_CXXNamespace);
    pb11_obj.value("CXIdxEntity_CXXNamespaceAlias",
                   CXIdxEntityKind::CXIdxEntity_CXXNamespaceAlias);
    pb11_obj.value("CXIdxEntity_CXXStaticVariable",
                   CXIdxEntityKind::CXIdxEntity_CXXStaticVariable);
    pb11_obj.value("CXIdxEntity_CXXStaticMethod",
                   CXIdxEntityKind::CXIdxEntity_CXXStaticMethod);
    pb11_obj.value("CXIdxEntity_CXXInstanceMethod",
                   CXIdxEntityKind::CXIdxEntity_CXXInstanceMethod);
    pb11_obj.value("CXIdxEntity_CXXConstructor",
                   CXIdxEntityKind::CXIdxEntity_CXXConstructor);
    pb11_obj.value("CXIdxEntity_CXXDestructor",
                   CXIdxEntityKind::CXIdxEntity_CXXDestructor);
    pb11_obj.value("CXIdxEntity_CXXConversionFunction",
                   CXIdxEntityKind::CXIdxEntity_CXXConversionFunction);
    pb11_obj.value("CXIdxEntity_CXXTypeAlias",
                   CXIdxEntityKind::CXIdxEntity_CXXTypeAlias);
    pb11_obj.value("CXIdxEntity_CXXInterface",
                   CXIdxEntityKind::CXIdxEntity_CXXInterface);
    pb11_obj.value("CXIdxEntity_CXXConcept",
                   CXIdxEntityKind::CXIdxEntity_CXXConcept);
  }

  static const char *Key() { return "CXIdxEntityKind"; }
};

struct Entity_CXIdxEntityKind
    : public Bind_CXIdxEntityKind<
          std::decay_t<pybind11::enum_<CXIdxEntityKind>>> {

  explicit Entity_CXIdxEntityKind(EntityScope parent_h)
      : Bind_CXIdxEntityKind<std::decay_t<pybind11::enum_<CXIdxEntityKind>>>(
            parent_h),
        handle{parent_h, "CXIdxEntityKind", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXIdxEntityKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxEntityKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxEntityLanguage
template <class Pybind11T> struct Bind_CXIdxEntityLanguage : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxEntityLanguage(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXIdxEntityLang_None",
                   CXIdxEntityLanguage::CXIdxEntityLang_None);
    pb11_obj.value("CXIdxEntityLang_C", CXIdxEntityLanguage::CXIdxEntityLang_C);
    pb11_obj.value("CXIdxEntityLang_ObjC",
                   CXIdxEntityLanguage::CXIdxEntityLang_ObjC);
    pb11_obj.value("CXIdxEntityLang_CXX",
                   CXIdxEntityLanguage::CXIdxEntityLang_CXX);
    pb11_obj.value("CXIdxEntityLang_Swift",
                   CXIdxEntityLanguage::CXIdxEntityLang_Swift);
  }

  static const char *Key() { return "CXIdxEntityLanguage"; }
};

struct Entity_CXIdxEntityLanguage
    : public Bind_CXIdxEntityLanguage<
          std::decay_t<pybind11::enum_<CXIdxEntityLanguage>>> {

  explicit Entity_CXIdxEntityLanguage(EntityScope parent_h)
      : Bind_CXIdxEntityLanguage<
            std::decay_t<pybind11::enum_<CXIdxEntityLanguage>>>(parent_h),
        handle{parent_h, "CXIdxEntityLanguage", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXIdxEntityLanguage> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxEntityLanguage

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxEntityCXXTemplateKind
template <class Pybind11T>
struct Bind_CXIdxEntityCXXTemplateKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxEntityCXXTemplateKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXIdxEntity_NonTemplate",
                   CXIdxEntityCXXTemplateKind::CXIdxEntity_NonTemplate);
    pb11_obj.value("CXIdxEntity_Template",
                   CXIdxEntityCXXTemplateKind::CXIdxEntity_Template);
    pb11_obj.value(
        "CXIdxEntity_TemplatePartialSpecialization",
        CXIdxEntityCXXTemplateKind::CXIdxEntity_TemplatePartialSpecialization);
    pb11_obj.value(
        "CXIdxEntity_TemplateSpecialization",
        CXIdxEntityCXXTemplateKind::CXIdxEntity_TemplateSpecialization);
  }

  static const char *Key() { return "CXIdxEntityCXXTemplateKind"; }
};

struct Entity_CXIdxEntityCXXTemplateKind
    : public Bind_CXIdxEntityCXXTemplateKind<
          std::decay_t<pybind11::enum_<CXIdxEntityCXXTemplateKind>>> {

  explicit Entity_CXIdxEntityCXXTemplateKind(EntityScope parent_h)
      : Bind_CXIdxEntityCXXTemplateKind<
            std::decay_t<pybind11::enum_<CXIdxEntityCXXTemplateKind>>>(
            parent_h),
        handle{parent_h, "CXIdxEntityCXXTemplateKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Extra C++ template information for an entity. This can apply to:
 * CXIdxEntity_Function
 * CXIdxEntity_CXXClass
 * CXIdxEntity_CXXStaticMethod
 * CXIdxEntity_CXXInstanceMethod
 * CXIdxEntity_CXXConstructor
 * CXIdxEntity_CXXConversionFunction
 * CXIdxEntity_CXXTypeAlias
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXIdxEntityCXXTemplateKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxEntityCXXTemplateKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxAttrKind
template <class Pybind11T> struct Bind_CXIdxAttrKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxAttrKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXIdxAttr_Unexposed", CXIdxAttrKind::CXIdxAttr_Unexposed);
    pb11_obj.value("CXIdxAttr_IBAction", CXIdxAttrKind::CXIdxAttr_IBAction);
    pb11_obj.value("CXIdxAttr_IBOutlet", CXIdxAttrKind::CXIdxAttr_IBOutlet);
    pb11_obj.value("CXIdxAttr_IBOutletCollection",
                   CXIdxAttrKind::CXIdxAttr_IBOutletCollection);
  }

  static const char *Key() { return "CXIdxAttrKind"; }
};

struct Entity_CXIdxAttrKind
    : public Bind_CXIdxAttrKind<std::decay_t<pybind11::enum_<CXIdxAttrKind>>> {

  explicit Entity_CXIdxAttrKind(EntityScope parent_h)
      : Bind_CXIdxAttrKind<std::decay_t<pybind11::enum_<CXIdxAttrKind>>>(
            parent_h),
        handle{parent_h, "CXIdxAttrKind", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXIdxAttrKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxAttrKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxAttrInfo
template <class Pybind11T> struct Bind_CXIdxAttrInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxAttrInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("kind", &CXIdxAttrInfo::kind);
    pb11_obj.def_readwrite("cursor", &CXIdxAttrInfo::cursor);
    pb11_obj.def_readwrite("loc", &CXIdxAttrInfo::loc);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxAttrInfo"; }
};

struct Entity_CXIdxAttrInfo
    : public Bind_CXIdxAttrInfo<std::decay_t<pybind11::class_<CXIdxAttrInfo>>> {

  explicit Entity_CXIdxAttrInfo(EntityScope parent_h)
      : Bind_CXIdxAttrInfo<std::decay_t<pybind11::class_<CXIdxAttrInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxAttrInfo", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxAttrInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxAttrInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxEntityInfo
template <class Pybind11T> struct Bind_CXIdxEntityInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxEntityInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("kind", &CXIdxEntityInfo::kind);
    pb11_obj.def_readwrite("templateKind", &CXIdxEntityInfo::templateKind);
    pb11_obj.def_readwrite("lang", &CXIdxEntityInfo::lang);
    pb11_obj.def_readwrite("name", &CXIdxEntityInfo::name);
    pb11_obj.def_readwrite("USR", &CXIdxEntityInfo::USR);
    pb11_obj.def_readwrite("cursor", &CXIdxEntityInfo::cursor);
    pb11_obj.def_readwrite("numAttributes", &CXIdxEntityInfo::numAttributes);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxEntityInfo"; }
};

struct Entity_CXIdxEntityInfo
    : public Bind_CXIdxEntityInfo<
          std::decay_t<pybind11::class_<CXIdxEntityInfo>>> {

  explicit Entity_CXIdxEntityInfo(EntityScope parent_h)
      : Bind_CXIdxEntityInfo<std::decay_t<pybind11::class_<CXIdxEntityInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxEntityInfo", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxEntityInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxEntityInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxContainerInfo
template <class Pybind11T> struct Bind_CXIdxContainerInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxContainerInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("cursor", &CXIdxContainerInfo::cursor);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxContainerInfo"; }
};

struct Entity_CXIdxContainerInfo
    : public Bind_CXIdxContainerInfo<
          std::decay_t<pybind11::class_<CXIdxContainerInfo>>> {

  explicit Entity_CXIdxContainerInfo(EntityScope parent_h)
      : Bind_CXIdxContainerInfo<
            std::decay_t<pybind11::class_<CXIdxContainerInfo>>>(parent_h),
        handle{parent_h, "CXIdxContainerInfo", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxContainerInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxContainerInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxIBOutletCollectionAttrInfo
template <class Pybind11T>
struct Bind_CXIdxIBOutletCollectionAttrInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxIBOutletCollectionAttrInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("attrInfo",
                           &CXIdxIBOutletCollectionAttrInfo::attrInfo);
    pb11_obj.def_readwrite("objcClass",
                           &CXIdxIBOutletCollectionAttrInfo::objcClass);
    pb11_obj.def_readwrite("classCursor",
                           &CXIdxIBOutletCollectionAttrInfo::classCursor);
    pb11_obj.def_readwrite("classLoc",
                           &CXIdxIBOutletCollectionAttrInfo::classLoc);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxIBOutletCollectionAttrInfo"; }
};

struct Entity_CXIdxIBOutletCollectionAttrInfo
    : public Bind_CXIdxIBOutletCollectionAttrInfo<
          std::decay_t<pybind11::class_<CXIdxIBOutletCollectionAttrInfo>>> {

  explicit Entity_CXIdxIBOutletCollectionAttrInfo(EntityScope parent_h)
      : Bind_CXIdxIBOutletCollectionAttrInfo<
            std::decay_t<pybind11::class_<CXIdxIBOutletCollectionAttrInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxIBOutletCollectionAttrInfo",
               pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxIBOutletCollectionAttrInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxIBOutletCollectionAttrInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxDeclInfoFlags
template <class Pybind11T> struct Bind_CXIdxDeclInfoFlags : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxDeclInfoFlags(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXIdxDeclFlag_Skipped",
                   CXIdxDeclInfoFlags::CXIdxDeclFlag_Skipped);
  }

  static const char *Key() { return "CXIdxDeclInfoFlags"; }
};

struct Entity_CXIdxDeclInfoFlags
    : public Bind_CXIdxDeclInfoFlags<
          std::decay_t<pybind11::enum_<CXIdxDeclInfoFlags>>> {

  explicit Entity_CXIdxDeclInfoFlags(EntityScope parent_h)
      : Bind_CXIdxDeclInfoFlags<
            std::decay_t<pybind11::enum_<CXIdxDeclInfoFlags>>>(parent_h),
        handle{parent_h, "CXIdxDeclInfoFlags", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXIdxDeclInfoFlags> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxDeclInfoFlags

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxDeclInfo
template <class Pybind11T> struct Bind_CXIdxDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("entityInfo", &CXIdxDeclInfo::entityInfo);
    pb11_obj.def_readwrite("cursor", &CXIdxDeclInfo::cursor);
    pb11_obj.def_readwrite("loc", &CXIdxDeclInfo::loc);
    pb11_obj.def_readwrite("semanticContainer",
                           &CXIdxDeclInfo::semanticContainer);
    pb11_obj.def_readwrite("lexicalContainer", &CXIdxDeclInfo::lexicalContainer,
                           R"_pb11_weaver(/**
   * Generally same as #semanticContainer but can be different in
   * cases like out-of-line C++ member functions.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("isRedeclaration", &CXIdxDeclInfo::isRedeclaration);
    pb11_obj.def_readwrite("isDefinition", &CXIdxDeclInfo::isDefinition);
    pb11_obj.def_readwrite("isContainer", &CXIdxDeclInfo::isContainer);
    pb11_obj.def_readwrite("declAsContainer", &CXIdxDeclInfo::declAsContainer);
    pb11_obj.def_readwrite("isImplicit", &CXIdxDeclInfo::isImplicit,
                           R"_pb11_weaver(/**
   * Whether the declaration exists in code or was created implicitly
   * by the compiler, e.g. implicit Objective-C methods for properties.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("numAttributes", &CXIdxDeclInfo::numAttributes);
    pb11_obj.def_readwrite("flags", &CXIdxDeclInfo::flags);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxDeclInfo"; }
};

struct Entity_CXIdxDeclInfo
    : public Bind_CXIdxDeclInfo<std::decay_t<pybind11::class_<CXIdxDeclInfo>>> {

  explicit Entity_CXIdxDeclInfo(EntityScope parent_h)
      : Bind_CXIdxDeclInfo<std::decay_t<pybind11::class_<CXIdxDeclInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxDeclInfo", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxDeclInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxObjCContainerKind
template <class Pybind11T>
struct Bind_CXIdxObjCContainerKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxObjCContainerKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXIdxObjCContainer_ForwardRef",
                   CXIdxObjCContainerKind::CXIdxObjCContainer_ForwardRef);
    pb11_obj.value("CXIdxObjCContainer_Interface",
                   CXIdxObjCContainerKind::CXIdxObjCContainer_Interface);
    pb11_obj.value("CXIdxObjCContainer_Implementation",
                   CXIdxObjCContainerKind::CXIdxObjCContainer_Implementation);
  }

  static const char *Key() { return "CXIdxObjCContainerKind"; }
};

struct Entity_CXIdxObjCContainerKind
    : public Bind_CXIdxObjCContainerKind<
          std::decay_t<pybind11::enum_<CXIdxObjCContainerKind>>> {

  explicit Entity_CXIdxObjCContainerKind(EntityScope parent_h)
      : Bind_CXIdxObjCContainerKind<
            std::decay_t<pybind11::enum_<CXIdxObjCContainerKind>>>(parent_h),
        handle{parent_h, "CXIdxObjCContainerKind", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXIdxObjCContainerKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxObjCContainerKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxObjCContainerDeclInfo
template <class Pybind11T>
struct Bind_CXIdxObjCContainerDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxObjCContainerDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("declInfo", &CXIdxObjCContainerDeclInfo::declInfo);
    pb11_obj.def_readwrite("kind", &CXIdxObjCContainerDeclInfo::kind);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxObjCContainerDeclInfo"; }
};

struct Entity_CXIdxObjCContainerDeclInfo
    : public Bind_CXIdxObjCContainerDeclInfo<
          std::decay_t<pybind11::class_<CXIdxObjCContainerDeclInfo>>> {

  explicit Entity_CXIdxObjCContainerDeclInfo(EntityScope parent_h)
      : Bind_CXIdxObjCContainerDeclInfo<
            std::decay_t<pybind11::class_<CXIdxObjCContainerDeclInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxObjCContainerDeclInfo",
               pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxObjCContainerDeclInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxObjCContainerDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxBaseClassInfo
template <class Pybind11T> struct Bind_CXIdxBaseClassInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxBaseClassInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("base", &CXIdxBaseClassInfo::base);
    pb11_obj.def_readwrite("cursor", &CXIdxBaseClassInfo::cursor);
    pb11_obj.def_readwrite("loc", &CXIdxBaseClassInfo::loc);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxBaseClassInfo"; }
};

struct Entity_CXIdxBaseClassInfo
    : public Bind_CXIdxBaseClassInfo<
          std::decay_t<pybind11::class_<CXIdxBaseClassInfo>>> {

  explicit Entity_CXIdxBaseClassInfo(EntityScope parent_h)
      : Bind_CXIdxBaseClassInfo<
            std::decay_t<pybind11::class_<CXIdxBaseClassInfo>>>(parent_h),
        handle{parent_h, "CXIdxBaseClassInfo", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxBaseClassInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxBaseClassInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxObjCProtocolRefInfo
template <class Pybind11T>
struct Bind_CXIdxObjCProtocolRefInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxObjCProtocolRefInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("protocol", &CXIdxObjCProtocolRefInfo::protocol);
    pb11_obj.def_readwrite("cursor", &CXIdxObjCProtocolRefInfo::cursor);
    pb11_obj.def_readwrite("loc", &CXIdxObjCProtocolRefInfo::loc);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxObjCProtocolRefInfo"; }
};

struct Entity_CXIdxObjCProtocolRefInfo
    : public Bind_CXIdxObjCProtocolRefInfo<
          std::decay_t<pybind11::class_<CXIdxObjCProtocolRefInfo>>> {

  explicit Entity_CXIdxObjCProtocolRefInfo(EntityScope parent_h)
      : Bind_CXIdxObjCProtocolRefInfo<
            std::decay_t<pybind11::class_<CXIdxObjCProtocolRefInfo>>>(parent_h),
        handle{parent_h, "CXIdxObjCProtocolRefInfo", pybind11::dynamic_attr()} {
  }

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxObjCProtocolRefInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxObjCProtocolRefInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxObjCProtocolRefListInfo
template <class Pybind11T>
struct Bind_CXIdxObjCProtocolRefListInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxObjCProtocolRefListInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("numProtocols",
                           &CXIdxObjCProtocolRefListInfo::numProtocols);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxObjCProtocolRefListInfo"; }
};

struct Entity_CXIdxObjCProtocolRefListInfo
    : public Bind_CXIdxObjCProtocolRefListInfo<
          std::decay_t<pybind11::class_<CXIdxObjCProtocolRefListInfo>>> {

  explicit Entity_CXIdxObjCProtocolRefListInfo(EntityScope parent_h)
      : Bind_CXIdxObjCProtocolRefListInfo<
            std::decay_t<pybind11::class_<CXIdxObjCProtocolRefListInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxObjCProtocolRefListInfo",
               pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxObjCProtocolRefListInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxObjCProtocolRefListInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxObjCInterfaceDeclInfo
template <class Pybind11T>
struct Bind_CXIdxObjCInterfaceDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxObjCInterfaceDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("containerInfo",
                           &CXIdxObjCInterfaceDeclInfo::containerInfo);
    pb11_obj.def_readwrite("superInfo", &CXIdxObjCInterfaceDeclInfo::superInfo);
    pb11_obj.def_readwrite("protocols", &CXIdxObjCInterfaceDeclInfo::protocols);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxObjCInterfaceDeclInfo"; }
};

struct Entity_CXIdxObjCInterfaceDeclInfo
    : public Bind_CXIdxObjCInterfaceDeclInfo<
          std::decay_t<pybind11::class_<CXIdxObjCInterfaceDeclInfo>>> {

  explicit Entity_CXIdxObjCInterfaceDeclInfo(EntityScope parent_h)
      : Bind_CXIdxObjCInterfaceDeclInfo<
            std::decay_t<pybind11::class_<CXIdxObjCInterfaceDeclInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxObjCInterfaceDeclInfo",
               pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxObjCInterfaceDeclInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxObjCInterfaceDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxObjCCategoryDeclInfo
template <class Pybind11T>
struct Bind_CXIdxObjCCategoryDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxObjCCategoryDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("containerInfo",
                           &CXIdxObjCCategoryDeclInfo::containerInfo);
    pb11_obj.def_readwrite("objcClass", &CXIdxObjCCategoryDeclInfo::objcClass);
    pb11_obj.def_readwrite("classCursor",
                           &CXIdxObjCCategoryDeclInfo::classCursor);
    pb11_obj.def_readwrite("classLoc", &CXIdxObjCCategoryDeclInfo::classLoc);
    pb11_obj.def_readwrite("protocols", &CXIdxObjCCategoryDeclInfo::protocols);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxObjCCategoryDeclInfo"; }
};

struct Entity_CXIdxObjCCategoryDeclInfo
    : public Bind_CXIdxObjCCategoryDeclInfo<
          std::decay_t<pybind11::class_<CXIdxObjCCategoryDeclInfo>>> {

  explicit Entity_CXIdxObjCCategoryDeclInfo(EntityScope parent_h)
      : Bind_CXIdxObjCCategoryDeclInfo<
            std::decay_t<pybind11::class_<CXIdxObjCCategoryDeclInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxObjCCategoryDeclInfo",
               pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxObjCCategoryDeclInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxObjCCategoryDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxObjCPropertyDeclInfo
template <class Pybind11T>
struct Bind_CXIdxObjCPropertyDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxObjCPropertyDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("declInfo", &CXIdxObjCPropertyDeclInfo::declInfo);
    pb11_obj.def_readwrite("getter", &CXIdxObjCPropertyDeclInfo::getter);
    pb11_obj.def_readwrite("setter", &CXIdxObjCPropertyDeclInfo::setter);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxObjCPropertyDeclInfo"; }
};

struct Entity_CXIdxObjCPropertyDeclInfo
    : public Bind_CXIdxObjCPropertyDeclInfo<
          std::decay_t<pybind11::class_<CXIdxObjCPropertyDeclInfo>>> {

  explicit Entity_CXIdxObjCPropertyDeclInfo(EntityScope parent_h)
      : Bind_CXIdxObjCPropertyDeclInfo<
            std::decay_t<pybind11::class_<CXIdxObjCPropertyDeclInfo>>>(
            parent_h),
        handle{parent_h, "CXIdxObjCPropertyDeclInfo",
               pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxObjCPropertyDeclInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxObjCPropertyDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxCXXClassDeclInfo
template <class Pybind11T>
struct Bind_CXIdxCXXClassDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxCXXClassDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("declInfo", &CXIdxCXXClassDeclInfo::declInfo);
    pb11_obj.def_readwrite("numBases", &CXIdxCXXClassDeclInfo::numBases);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxCXXClassDeclInfo"; }
};

struct Entity_CXIdxCXXClassDeclInfo
    : public Bind_CXIdxCXXClassDeclInfo<
          std::decay_t<pybind11::class_<CXIdxCXXClassDeclInfo>>> {

  explicit Entity_CXIdxCXXClassDeclInfo(EntityScope parent_h)
      : Bind_CXIdxCXXClassDeclInfo<
            std::decay_t<pybind11::class_<CXIdxCXXClassDeclInfo>>>(parent_h),
        handle{parent_h, "CXIdxCXXClassDeclInfo", pybind11::dynamic_attr()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxCXXClassDeclInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxCXXClassDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxEntityRefKind
template <class Pybind11T> struct Bind_CXIdxEntityRefKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxEntityRefKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXIdxEntityRef_Direct",
                   CXIdxEntityRefKind::CXIdxEntityRef_Direct, R"_pb11_weaver(/**
   * The entity is referenced directly in user's code.
   */)_pb11_weaver");
    pb11_obj.value("CXIdxEntityRef_Implicit",
                   CXIdxEntityRefKind::CXIdxEntityRef_Implicit,
                   R"_pb11_weaver(/**
   * An implicit reference, e.g. a reference of an Objective-C method
   * via the dot syntax.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXIdxEntityRefKind"; }
};

struct Entity_CXIdxEntityRefKind
    : public Bind_CXIdxEntityRefKind<
          std::decay_t<pybind11::enum_<CXIdxEntityRefKind>>> {

  explicit Entity_CXIdxEntityRefKind(EntityScope parent_h)
      : Bind_CXIdxEntityRefKind<
            std::decay_t<pybind11::enum_<CXIdxEntityRefKind>>>(parent_h),
        handle{parent_h, "CXIdxEntityRefKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Data for IndexerCallbacks#indexEntityReference.
 *
 * This may be deprecated in a future version as this duplicates
 * the \c CXSymbolRole_Implicit bit in \c CXSymbolRole.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXIdxEntityRefKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxEntityRefKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXSymbolRole
template <class Pybind11T> struct Bind_CXSymbolRole : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXSymbolRole(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXSymbolRole_None", CXSymbolRole::CXSymbolRole_None);
    pb11_obj.value("CXSymbolRole_Declaration",
                   CXSymbolRole::CXSymbolRole_Declaration);
    pb11_obj.value("CXSymbolRole_Definition",
                   CXSymbolRole::CXSymbolRole_Definition);
    pb11_obj.value("CXSymbolRole_Reference",
                   CXSymbolRole::CXSymbolRole_Reference);
    pb11_obj.value("CXSymbolRole_Read", CXSymbolRole::CXSymbolRole_Read);
    pb11_obj.value("CXSymbolRole_Write", CXSymbolRole::CXSymbolRole_Write);
    pb11_obj.value("CXSymbolRole_Call", CXSymbolRole::CXSymbolRole_Call);
    pb11_obj.value("CXSymbolRole_Dynamic", CXSymbolRole::CXSymbolRole_Dynamic);
    pb11_obj.value("CXSymbolRole_AddressOf",
                   CXSymbolRole::CXSymbolRole_AddressOf);
    pb11_obj.value("CXSymbolRole_Implicit",
                   CXSymbolRole::CXSymbolRole_Implicit);
  }

  static const char *Key() { return "CXSymbolRole"; }
};

struct Entity_CXSymbolRole
    : public Bind_CXSymbolRole<std::decay_t<pybind11::enum_<CXSymbolRole>>> {

  explicit Entity_CXSymbolRole(EntityScope parent_h)
      : Bind_CXSymbolRole<std::decay_t<pybind11::enum_<CXSymbolRole>>>(
            parent_h),
        handle{parent_h, "CXSymbolRole", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Roles that are attributed to symbol occurrences.
 *
 * Internal: this currently mirrors low 9 bits of clang::index::SymbolRole with
 * higher bits zeroed. These high bits may be exposed in the future.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXSymbolRole> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXSymbolRole

#ifndef PB11_WEAVER_DISABLE_Entity_CXIdxEntityRefInfo
template <class Pybind11T> struct Bind_CXIdxEntityRefInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIdxEntityRefInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * Sets of symbol roles of the reference.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
    pb11_obj.def_readwrite("kind", &CXIdxEntityRefInfo::kind);
    pb11_obj.def_readwrite("cursor", &CXIdxEntityRefInfo::cursor,
                           R"_pb11_weaver(/**
   * Reference cursor.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("loc", &CXIdxEntityRefInfo::loc);
    pb11_obj.def_readwrite("referencedEntity",
                           &CXIdxEntityRefInfo::referencedEntity,
                           R"_pb11_weaver(/**
   * The entity that gets referenced.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("parentEntity", &CXIdxEntityRefInfo::parentEntity,
                           R"_pb11_weaver(/**
   * Immediate "parent" of the reference. For example:
   *
   * \code
   * Foo *var;
   * \endcode
   *
   * The parent of reference of type 'Foo' is the variable 'var'.
   * For references inside statement bodies of functions/methods,
   * the parentEntity will be the function/method.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("container", &CXIdxEntityRefInfo::container,
                           R"_pb11_weaver(/**
   * Lexical container context of the reference.
   */)_pb11_weaver");
    pb11_obj.def_readwrite("role", &CXIdxEntityRefInfo::role, R"_pb11_weaver(/**
   * Sets of symbol roles of the reference.
   */)_pb11_weaver");
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXIdxEntityRefInfo"; }
};

struct Entity_CXIdxEntityRefInfo
    : public Bind_CXIdxEntityRefInfo<
          std::decay_t<pybind11::class_<CXIdxEntityRefInfo>>> {

  explicit Entity_CXIdxEntityRefInfo(EntityScope parent_h)
      : Bind_CXIdxEntityRefInfo<
            std::decay_t<pybind11::class_<CXIdxEntityRefInfo>>>(parent_h),
        handle{parent_h, "CXIdxEntityRefInfo", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * Data for IndexerCallbacks#indexEntityReference.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXIdxEntityRefInfo> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIdxEntityRefInfo

#ifndef PB11_WEAVER_DISABLE_Entity_IndexerCallbacks
template <class Pybind11T> struct Bind_IndexerCallbacks : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_IndexerCallbacks(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>(), R"_pb11_weaver(/**
   * Called to index a reference of an entity.
   */)_pb11_weaver");
    Pybind11WeaverBindAllMethods(pb11_obj);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "IndexerCallbacks"; }
};

struct Entity_IndexerCallbacks
    : public Bind_IndexerCallbacks<
          std::decay_t<pybind11::class_<IndexerCallbacks>>> {

  explicit Entity_IndexerCallbacks(EntityScope parent_h)
      : Bind_IndexerCallbacks<std::decay_t<pybind11::class_<IndexerCallbacks>>>(
            parent_h),
        handle{parent_h, "IndexerCallbacks", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * A group of callbacks used by #clang_indexSourceFile and
 * #clang_indexTranslationUnit.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<IndexerCallbacks> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_IndexerCallbacks

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_isEntityObjCContainerKind
template <class Pybind11T>
struct Bind_clang_index_isEntityObjCContainerKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_isEntityObjCContainerKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_index_isEntityObjCContainerKind",
                 static_cast<int (*)(CXIdxEntityKind)>(
                     &clang_index_isEntityObjCContainerKind));
  }

  static const char *Key() { return "clang_index_isEntityObjCContainerKind"; }
};

struct Entity_clang_index_isEntityObjCContainerKind
    : public Bind_clang_index_isEntityObjCContainerKind<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_isEntityObjCContainerKind(EntityScope parent_h)
      : Bind_clang_index_isEntityObjCContainerKind<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_isEntityObjCContainerKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getObjCContainerDeclInfo
template <class Pybind11T>
struct Bind_clang_index_getObjCContainerDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getObjCContainerDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getObjCContainerDeclInfo",
        static_cast<
            const CXIdxObjCContainerDeclInfo *(*)(const CXIdxDeclInfo *)>(
            &clang_index_getObjCContainerDeclInfo));
  }

  static const char *Key() { return "clang_index_getObjCContainerDeclInfo"; }
};

struct Entity_clang_index_getObjCContainerDeclInfo
    : public Bind_clang_index_getObjCContainerDeclInfo<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getObjCContainerDeclInfo(EntityScope parent_h)
      : Bind_clang_index_getObjCContainerDeclInfo<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getObjCContainerDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getObjCInterfaceDeclInfo
template <class Pybind11T>
struct Bind_clang_index_getObjCInterfaceDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getObjCInterfaceDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getObjCInterfaceDeclInfo",
        static_cast<
            const CXIdxObjCInterfaceDeclInfo *(*)(const CXIdxDeclInfo *)>(
            &clang_index_getObjCInterfaceDeclInfo));
  }

  static const char *Key() { return "clang_index_getObjCInterfaceDeclInfo"; }
};

struct Entity_clang_index_getObjCInterfaceDeclInfo
    : public Bind_clang_index_getObjCInterfaceDeclInfo<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getObjCInterfaceDeclInfo(EntityScope parent_h)
      : Bind_clang_index_getObjCInterfaceDeclInfo<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getObjCInterfaceDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getObjCCategoryDeclInfo
template <class Pybind11T>
struct Bind_clang_index_getObjCCategoryDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getObjCCategoryDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getObjCCategoryDeclInfo",
        static_cast<
            const CXIdxObjCCategoryDeclInfo *(*)(const CXIdxDeclInfo *)>(
            &clang_index_getObjCCategoryDeclInfo));
  }

  static const char *Key() { return "clang_index_getObjCCategoryDeclInfo"; }
};

struct Entity_clang_index_getObjCCategoryDeclInfo
    : public Bind_clang_index_getObjCCategoryDeclInfo<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getObjCCategoryDeclInfo(EntityScope parent_h)
      : Bind_clang_index_getObjCCategoryDeclInfo<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getObjCCategoryDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getObjCProtocolRefListInfo
template <class Pybind11T>
struct Bind_clang_index_getObjCProtocolRefListInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getObjCProtocolRefListInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getObjCProtocolRefListInfo",
        static_cast<
            const CXIdxObjCProtocolRefListInfo *(*)(const CXIdxDeclInfo *)>(
            &clang_index_getObjCProtocolRefListInfo));
  }

  static const char *Key() { return "clang_index_getObjCProtocolRefListInfo"; }
};

struct Entity_clang_index_getObjCProtocolRefListInfo
    : public Bind_clang_index_getObjCProtocolRefListInfo<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getObjCProtocolRefListInfo(EntityScope parent_h)
      : Bind_clang_index_getObjCProtocolRefListInfo<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getObjCProtocolRefListInfo

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getObjCPropertyDeclInfo
template <class Pybind11T>
struct Bind_clang_index_getObjCPropertyDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getObjCPropertyDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getObjCPropertyDeclInfo",
        static_cast<
            const CXIdxObjCPropertyDeclInfo *(*)(const CXIdxDeclInfo *)>(
            &clang_index_getObjCPropertyDeclInfo));
  }

  static const char *Key() { return "clang_index_getObjCPropertyDeclInfo"; }
};

struct Entity_clang_index_getObjCPropertyDeclInfo
    : public Bind_clang_index_getObjCPropertyDeclInfo<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getObjCPropertyDeclInfo(EntityScope parent_h)
      : Bind_clang_index_getObjCPropertyDeclInfo<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getObjCPropertyDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getIBOutletCollectionAttrInfo
template <class Pybind11T>
struct Bind_clang_index_getIBOutletCollectionAttrInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getIBOutletCollectionAttrInfo(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getIBOutletCollectionAttrInfo",
        static_cast<
            const CXIdxIBOutletCollectionAttrInfo *(*)(const CXIdxAttrInfo *)>(
            &clang_index_getIBOutletCollectionAttrInfo));
  }

  static const char *Key() {
    return "clang_index_getIBOutletCollectionAttrInfo";
  }
};

struct Entity_clang_index_getIBOutletCollectionAttrInfo
    : public Bind_clang_index_getIBOutletCollectionAttrInfo<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getIBOutletCollectionAttrInfo(
      EntityScope parent_h)
      : Bind_clang_index_getIBOutletCollectionAttrInfo<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getIBOutletCollectionAttrInfo

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getCXXClassDeclInfo
template <class Pybind11T>
struct Bind_clang_index_getCXXClassDeclInfo : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getCXXClassDeclInfo(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getCXXClassDeclInfo",
        static_cast<const CXIdxCXXClassDeclInfo *(*)(const CXIdxDeclInfo *)>(
            &clang_index_getCXXClassDeclInfo));
  }

  static const char *Key() { return "clang_index_getCXXClassDeclInfo"; }
};

struct Entity_clang_index_getCXXClassDeclInfo
    : public Bind_clang_index_getCXXClassDeclInfo<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getCXXClassDeclInfo(EntityScope parent_h)
      : Bind_clang_index_getCXXClassDeclInfo<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getCXXClassDeclInfo

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getClientContainer
template <class Pybind11T>
struct Bind_clang_index_getClientContainer : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getClientContainer(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getClientContainer",
        [=](const CXIdxContainerInfo *arg0) {
          return pybind11_weaver::WrapP<void *>(
                     clang_index_getClientContainer(arg0))
              .release();
        },
        R"_pb11_weaver(/**
 * For retrieving a custom CXIdxClientContainer attached to a
 * container.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_index_getClientContainer"; }
};

struct Entity_clang_index_getClientContainer
    : public Bind_clang_index_getClientContainer<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getClientContainer(EntityScope parent_h)
      : Bind_clang_index_getClientContainer<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getClientContainer

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_setClientContainer
template <class Pybind11T>
struct Bind_clang_index_setClientContainer : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_setClientContainer(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_setClientContainer",
        [=](const CXIdxContainerInfo *arg0,
            pybind11_weaver::PointerWrapper<void *> *arg1) {
          return clang_index_setClientContainer(arg0, (arg1)->ptr);
        },
        R"_pb11_weaver(/**
 * For setting a custom CXIdxClientContainer attached to a
 * container.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_index_setClientContainer"; }
};

struct Entity_clang_index_setClientContainer
    : public Bind_clang_index_setClientContainer<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_setClientContainer(EntityScope parent_h)
      : Bind_clang_index_setClientContainer<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_setClientContainer

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_getClientEntity
template <class Pybind11T>
struct Bind_clang_index_getClientEntity : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_getClientEntity(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_getClientEntity",
        [=](const CXIdxEntityInfo *arg0) {
          return pybind11_weaver::WrapP<void *>(
                     clang_index_getClientEntity(arg0))
              .release();
        },
        R"_pb11_weaver(/**
 * For retrieving a custom CXIdxClientEntity attached to an entity.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_index_getClientEntity"; }
};

struct Entity_clang_index_getClientEntity
    : public Bind_clang_index_getClientEntity<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_getClientEntity(EntityScope parent_h)
      : Bind_clang_index_getClientEntity<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_getClientEntity

#ifndef PB11_WEAVER_DISABLE_Entity_clang_index_setClientEntity
template <class Pybind11T>
struct Bind_clang_index_setClientEntity : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_index_setClientEntity(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_index_setClientEntity",
        [=](const CXIdxEntityInfo *arg0,
            pybind11_weaver::PointerWrapper<void *> *arg1) {
          return clang_index_setClientEntity(arg0, (arg1)->ptr);
        },
        R"_pb11_weaver(/**
 * For setting a custom CXIdxClientEntity attached to an entity.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_index_setClientEntity"; }
};

struct Entity_clang_index_setClientEntity
    : public Bind_clang_index_setClientEntity<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_index_setClientEntity(EntityScope parent_h)
      : Bind_clang_index_setClientEntity<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_index_setClientEntity

#ifndef PB11_WEAVER_DISABLE_Entity_clang_IndexAction_create
template <class Pybind11T>
struct Bind_clang_IndexAction_create : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_IndexAction_create(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_IndexAction_create",
        [=](pybind11_weaver::PointerWrapper<void *> *CIdx) {
          return pybind11_weaver::WrapP<void *>(
                     clang_IndexAction_create((CIdx)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * An indexing action/session, to be applied to one or multiple
 * translation units.
 *
 * \param CIdx The index object with which the index action will be associated.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_IndexAction_create"; }
};

struct Entity_clang_IndexAction_create
    : public Bind_clang_IndexAction_create<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_IndexAction_create(EntityScope parent_h)
      : Bind_clang_IndexAction_create<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_IndexAction_create

#ifndef PB11_WEAVER_DISABLE_Entity_clang_IndexAction_dispose
template <class Pybind11T>
struct Bind_clang_IndexAction_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_IndexAction_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_IndexAction_dispose",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0) {
          return clang_IndexAction_dispose((arg0)->ptr);
        },
        R"_pb11_weaver(/**
 * Destroy the given index action.
 *
 * The index action must not be destroyed until all of the translation units
 * created within that index action have been destroyed.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_IndexAction_dispose"; }
};

struct Entity_clang_IndexAction_dispose
    : public Bind_clang_IndexAction_dispose<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_IndexAction_dispose(EntityScope parent_h)
      : Bind_clang_IndexAction_dispose<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_IndexAction_dispose

#ifndef PB11_WEAVER_DISABLE_Entity_CXIndexOptFlags
template <class Pybind11T> struct Bind_CXIndexOptFlags : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXIndexOptFlags(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXIndexOpt_None", CXIndexOptFlags::CXIndexOpt_None,
                   R"_pb11_weaver(/**
   * Used to indicate that no special indexing options are needed.
   */)_pb11_weaver");
    pb11_obj.value("CXIndexOpt_SuppressRedundantRefs",
                   CXIndexOptFlags::CXIndexOpt_SuppressRedundantRefs,
                   R"_pb11_weaver(/**
   * Used to indicate that IndexerCallbacks#indexEntityReference should
   * be invoked for only one reference of an entity per source file that does
   * not also include a declaration/definition of the entity.
   */)_pb11_weaver");
    pb11_obj.value("CXIndexOpt_IndexFunctionLocalSymbols",
                   CXIndexOptFlags::CXIndexOpt_IndexFunctionLocalSymbols,
                   R"_pb11_weaver(/**
   * Function-local symbols should be indexed. If this is not set
   * function-local symbols will be ignored.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXIndexOpt_IndexImplicitTemplateInstantiations",
        CXIndexOptFlags::CXIndexOpt_IndexImplicitTemplateInstantiations,
        R"_pb11_weaver(/**
   * Implicit function/class template instantiations should be indexed.
   * If this is not set, implicit instantiations will be ignored.
   */)_pb11_weaver");
    pb11_obj.value("CXIndexOpt_SuppressWarnings",
                   CXIndexOptFlags::CXIndexOpt_SuppressWarnings,
                   R"_pb11_weaver(/**
   * Suppress all compiler warnings when parsing for indexing.
   */)_pb11_weaver");
    pb11_obj.value("CXIndexOpt_SkipParsedBodiesInSession",
                   CXIndexOptFlags::CXIndexOpt_SkipParsedBodiesInSession,
                   R"_pb11_weaver(/**
   * Skip a function/method body that was already parsed during an
   * indexing session associated with a \c CXIndexAction object.
   * Bodies in system headers are always skipped.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXIndexOptFlags"; }
};

struct Entity_CXIndexOptFlags
    : public Bind_CXIndexOptFlags<
          std::decay_t<pybind11::enum_<CXIndexOptFlags>>> {

  explicit Entity_CXIndexOptFlags(EntityScope parent_h)
      : Bind_CXIndexOptFlags<std::decay_t<pybind11::enum_<CXIndexOptFlags>>>(
            parent_h),
        handle{parent_h, "CXIndexOptFlags", pybind11::arithmetic()} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXIndexOptFlags> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXIndexOptFlags

#ifndef PB11_WEAVER_DISABLE_Entity_clang_indexSourceFile
template <class Pybind11T>
struct Bind_clang_indexSourceFile : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_indexSourceFile(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_indexSourceFile",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0,
            pybind11_weaver::PointerWrapper<void *> *client_data,
            IndexerCallbacks *index_callbacks,
            unsigned int index_callbacks_size, unsigned int index_options,
            const char *source_filename,
            pybind11_weaver::PointerWrapper<const char *const *>
                *command_line_args,
            int num_command_line_args, CXUnsavedFile *unsaved_files,
            unsigned int num_unsaved_files,
            pybind11_weaver::PointerWrapper<CXTranslationUnitImpl **> *out_TU,
            unsigned int TU_options) {
          return clang_indexSourceFile(
              (arg0)->ptr, (client_data)->ptr, index_callbacks,
              index_callbacks_size, index_options, source_filename,
              (command_line_args)->ptr, num_command_line_args, unsaved_files,
              num_unsaved_files, (out_TU)->ptr, TU_options);
        },
        R"_pb11_weaver(/**
 * Index the given source file and the translation unit corresponding
 * to that file via callbacks implemented through #IndexerCallbacks.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the invoked callbacks.
 *
 * \param index_callbacks Pointer to indexing callbacks that the client
 * implements.
 *
 * \param index_callbacks_size Size of #IndexerCallbacks structure that gets
 * passed in index_callbacks.
 *
 * \param index_options A bitmask of options that affects how indexing is
 * performed. This should be a bitwise OR of the CXIndexOpt_XXX flags.
 *
 * \param[out] out_TU pointer to store a \c CXTranslationUnit that can be
 * reused after indexing is finished. Set to \c NULL if you do not require it.
 *
 * \returns 0 on success or if there were errors from which the compiler could
 * recover.  If there is a failure from which there is no recovery, returns
 * a non-zero \c CXErrorCode.
 *
 * The rest of the parameters are the same as #clang_parseTranslationUnit.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_indexSourceFile"; }
};

struct Entity_clang_indexSourceFile
    : public Bind_clang_indexSourceFile<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_indexSourceFile(EntityScope parent_h)
      : Bind_clang_indexSourceFile<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_indexSourceFile

#ifndef PB11_WEAVER_DISABLE_Entity_clang_indexSourceFileFullArgv
template <class Pybind11T>
struct Bind_clang_indexSourceFileFullArgv : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_indexSourceFileFullArgv(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_indexSourceFileFullArgv",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0,
            pybind11_weaver::PointerWrapper<void *> *client_data,
            IndexerCallbacks *index_callbacks,
            unsigned int index_callbacks_size, unsigned int index_options,
            const char *source_filename,
            pybind11_weaver::PointerWrapper<const char *const *>
                *command_line_args,
            int num_command_line_args, CXUnsavedFile *unsaved_files,
            unsigned int num_unsaved_files,
            pybind11_weaver::PointerWrapper<CXTranslationUnitImpl **> *out_TU,
            unsigned int TU_options) {
          return clang_indexSourceFileFullArgv(
              (arg0)->ptr, (client_data)->ptr, index_callbacks,
              index_callbacks_size, index_options, source_filename,
              (command_line_args)->ptr, num_command_line_args, unsaved_files,
              num_unsaved_files, (out_TU)->ptr, TU_options);
        },
        R"_pb11_weaver(/**
 * Same as clang_indexSourceFile but requires a full command line
 * for \c command_line_args including argv[0]. This is useful if the standard
 * library paths are relative to the binary.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_indexSourceFileFullArgv"; }
};

struct Entity_clang_indexSourceFileFullArgv
    : public Bind_clang_indexSourceFileFullArgv<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_indexSourceFileFullArgv(EntityScope parent_h)
      : Bind_clang_indexSourceFileFullArgv<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_indexSourceFileFullArgv

#ifndef PB11_WEAVER_DISABLE_Entity_clang_indexTranslationUnit
template <class Pybind11T>
struct Bind_clang_indexTranslationUnit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_indexTranslationUnit(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_indexTranslationUnit",
        [=](pybind11_weaver::PointerWrapper<void *> *arg0,
            pybind11_weaver::PointerWrapper<void *> *client_data,
            IndexerCallbacks *index_callbacks,
            unsigned int index_callbacks_size, unsigned int index_options,
            pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *arg5) {
          return clang_indexTranslationUnit(
              (arg0)->ptr, (client_data)->ptr, index_callbacks,
              index_callbacks_size, index_options, (arg5)->ptr);
        },
        R"_pb11_weaver(/**
 * Index the given translation unit via callbacks implemented through
 * #IndexerCallbacks.
 *
 * The order of callback invocations is not guaranteed to be the same as
 * when indexing a source file. The high level order will be:
 *
 *   -Preprocessor callbacks invocations
 *   -Declaration/reference callbacks invocations
 *   -Diagnostic callback invocations
 *
 * The parameters are the same as #clang_indexSourceFile.
 *
 * \returns If there is a failure from which there is no recovery, returns
 * non-zero, otherwise returns 0.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_indexTranslationUnit"; }
};

struct Entity_clang_indexTranslationUnit
    : public Bind_clang_indexTranslationUnit<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_indexTranslationUnit(EntityScope parent_h)
      : Bind_clang_indexTranslationUnit<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_indexTranslationUnit

#ifndef PB11_WEAVER_DISABLE_Entity_clang_indexLoc_getFileLocation
template <class Pybind11T>
struct Bind_clang_indexLoc_getFileLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_indexLoc_getFileLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_indexLoc_getFileLocation",
        [=](CXIdxLoc loc, pybind11_weaver::PointerWrapper<void **> *indexFile,
            pybind11_weaver::PointerWrapper<void **> *file, unsigned int *line,
            unsigned int *column, unsigned int *offset) {
          return clang_indexLoc_getFileLocation(
              loc, (indexFile)->ptr, (file)->ptr, line, column, offset);
        },
        R"_pb11_weaver(/**
 * Retrieve the CXIdxFile, file, line, column, and offset represented by
 * the given CXIdxLoc.
 *
 * If the location refers into a macro expansion, retrieves the
 * location of the macro expansion and if it refers into a macro argument
 * retrieves the location of the argument.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_indexLoc_getFileLocation"; }
};

struct Entity_clang_indexLoc_getFileLocation
    : public Bind_clang_indexLoc_getFileLocation<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_indexLoc_getFileLocation(EntityScope parent_h)
      : Bind_clang_indexLoc_getFileLocation<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_indexLoc_getFileLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_indexLoc_getCXSourceLocation
template <class Pybind11T>
struct Bind_clang_indexLoc_getCXSourceLocation : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_indexLoc_getCXSourceLocation(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_indexLoc_getCXSourceLocation",
                 static_cast<CXSourceLocation (*)(CXIdxLoc)>(
                     &clang_indexLoc_getCXSourceLocation),
                 R"_pb11_weaver(/**
 * Retrieve the CXSourceLocation represented by the given CXIdxLoc.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_indexLoc_getCXSourceLocation"; }
};

struct Entity_clang_indexLoc_getCXSourceLocation
    : public Bind_clang_indexLoc_getCXSourceLocation<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_indexLoc_getCXSourceLocation(EntityScope parent_h)
      : Bind_clang_indexLoc_getCXSourceLocation<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_indexLoc_getCXSourceLocation

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Type_visitFields
template <class Pybind11T>
struct Bind_clang_Type_visitFields : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Type_visitFields(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Type_visitFields",
        [=](CXType T,
            std::function<CXVisitorResult(
                CXCursor, pybind11_weaver::PointerWrapper<void *> *)>
                visitor,
            pybind11_weaver::PointerWrapper<void *> *client_data) {
          return clang_Type_visitFields(
              T,
              pybind11_weaver::FnPointerWrapper<
                  CXVisitorResult, CXCursor,
                  pybind11_weaver::PointerWrapper<void *> *>::
                  GetCptr<CXVisitorResult, CXCursor, void *>::Run(
                      (visitor), pybind11_weaver::Guardian(),
                      [](CXCursor arg0_0, void *arg0_1) {
                        auto &to_call = pybind11_weaver::FnPointerWrapper<
                            CXVisitorResult, CXCursor,
                            pybind11_weaver::PointerWrapper<void *>
                                *>::FnProxy(4);
                        return to_call(
                            arg0_0,
                            pybind11_weaver::WrapP<void *>(arg0_1).get());
                      },
                      4),
              (client_data)->ptr);
        },
        R"_pb11_weaver(/**
 * Visit the fields of a particular type.
 *
 * This function visits all the direct fields of the given cursor,
 * invoking the given \p visitor function with the cursors of each
 * visited field. The traversal may be ended prematurely, if
 * the visitor returns \c CXFieldVisit_Break.
 *
 * \param T the record type whose field may be visited.
 *
 * \param visitor the visitor function that will be invoked for each
 * field of \p T.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the visitor each time it is invoked.
 *
 * \returns a non-zero value if the traversal was terminated
 * prematurely by the visitor returning \c CXFieldVisit_Break.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Type_visitFields"; }
};

struct Entity_clang_Type_visitFields
    : public Bind_clang_Type_visitFields<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Type_visitFields(EntityScope parent_h)
      : Bind_clang_Type_visitFields<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Type_visitFields

#ifndef PB11_WEAVER_DISABLE_Entity_CXComment
template <class Pybind11T> struct Bind_CXComment : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXComment(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(pybind11::init<>());
    Pybind11WeaverBindAllMethods(pb11_obj);
  }

  void Pybind11WeaverBindAllMethods(Pybind11T &obj) {}

  static const char *Key() { return "CXComment"; }
};

struct Entity_CXComment
    : public Bind_CXComment<std::decay_t<pybind11::class_<CXComment>>> {

  explicit Entity_CXComment(EntityScope parent_h)
      : Bind_CXComment<std::decay_t<pybind11::class_<CXComment>>>(parent_h),
        handle{parent_h, "CXComment", pybind11::dynamic_attr(),
               R"_pb11_weaver(/**
 * A parsed comment.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::class_<CXComment> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXComment

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Cursor_getParsedComment
template <class Pybind11T>
struct Bind_clang_Cursor_getParsedComment : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Cursor_getParsedComment(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Cursor_getParsedComment",
        static_cast<CXComment (*)(CXCursor)>(&clang_Cursor_getParsedComment),
        R"_pb11_weaver(/**
 * Given a cursor that represents a documentable entity (e.g.,
 * declaration), return the associated parsed comment as a
 * \c CXComment_FullComment AST node.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Cursor_getParsedComment"; }
};

struct Entity_clang_Cursor_getParsedComment
    : public Bind_clang_Cursor_getParsedComment<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Cursor_getParsedComment(EntityScope parent_h)
      : Bind_clang_Cursor_getParsedComment<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Cursor_getParsedComment

#ifndef PB11_WEAVER_DISABLE_Entity_CXCommentKind
template <class Pybind11T> struct Bind_CXCommentKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCommentKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXComment_Null", CXCommentKind::CXComment_Null,
                   R"_pb11_weaver(/**
   * Null comment.  No AST node is constructed at the requested location
   * because there is no text or a syntax error.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_Text", CXCommentKind::CXComment_Text,
                   R"_pb11_weaver(/**
   * Plain text.  Inline content.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_InlineCommand",
                   CXCommentKind::CXComment_InlineCommand, R"_pb11_weaver(/**
   * A command with word-like arguments that is considered inline content.
   *
   * For example: \\c command.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_HTMLStartTag",
                   CXCommentKind::CXComment_HTMLStartTag, R"_pb11_weaver(/**
   * HTML start tag with attributes (name-value pairs).  Considered
   * inline content.
   *
   * For example:
   * \verbatim
   * <br> <br /> <a href="http://example.org/">
   * \endverbatim
   */)_pb11_weaver");
    pb11_obj.value("CXComment_HTMLEndTag", CXCommentKind::CXComment_HTMLEndTag,
                   R"_pb11_weaver(/**
   * HTML end tag.  Considered inline content.
   *
   * For example:
   * \verbatim
   * </a>
   * \endverbatim
   */)_pb11_weaver");
    pb11_obj.value("CXComment_Paragraph", CXCommentKind::CXComment_Paragraph,
                   R"_pb11_weaver(/**
   * A paragraph, contains inline comment.  The paragraph itself is
   * block content.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_BlockCommand",
                   CXCommentKind::CXComment_BlockCommand, R"_pb11_weaver(/**
   * A command that has zero or more word-like arguments (number of
   * word-like arguments depends on command name) and a paragraph as an
   * argument.  Block command is block content.
   *
   * Paragraph argument is also a child of the block command.
   *
   * For example: \has 0 word-like arguments and a paragraph argument.
   *
   * AST nodes of special kinds that parser knows about (e. g., \\param
   * command) have their own node kinds.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_ParamCommand",
                   CXCommentKind::CXComment_ParamCommand, R"_pb11_weaver(/**
   * A \\param or \\arg command that describes the function parameter
   * (name, passing direction, description).
   *
   * For example: \\param [in] ParamName description.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_TParamCommand",
                   CXCommentKind::CXComment_TParamCommand, R"_pb11_weaver(/**
   * A \\tparam command that describes a template parameter (name and
   * description).
   *
   * For example: \\tparam T description.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_VerbatimBlockCommand",
                   CXCommentKind::CXComment_VerbatimBlockCommand,
                   R"_pb11_weaver(/**
   * A verbatim block command (e. g., preformatted code).  Verbatim
   * block has an opening and a closing command and contains multiple lines of
   * text (\c CXComment_VerbatimBlockLine child nodes).
   *
   * For example:
   * \\verbatim
   * aaa
   * \\endverbatim
   */)_pb11_weaver");
    pb11_obj.value("CXComment_VerbatimBlockLine",
                   CXCommentKind::CXComment_VerbatimBlockLine,
                   R"_pb11_weaver(/**
   * A line of text that is contained within a
   * CXComment_VerbatimBlockCommand node.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_VerbatimLine",
                   CXCommentKind::CXComment_VerbatimLine, R"_pb11_weaver(/**
   * A verbatim line command.  Verbatim line has an opening command,
   * a single line of text (up to the newline after the opening command) and
   * has no closing command.
   */)_pb11_weaver");
    pb11_obj.value("CXComment_FullComment",
                   CXCommentKind::CXComment_FullComment, R"_pb11_weaver(/**
   * A full comment attached to a declaration, contains block content.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCommentKind"; }
};

struct Entity_CXCommentKind
    : public Bind_CXCommentKind<std::decay_t<pybind11::enum_<CXCommentKind>>> {

  explicit Entity_CXCommentKind(EntityScope parent_h)
      : Bind_CXCommentKind<std::decay_t<pybind11::enum_<CXCommentKind>>>(
            parent_h),
        handle{parent_h, "CXCommentKind", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes the type of the comment AST node (\c CXComment).  A comment
 * node can be considered block content (e. g., paragraph), inline content
 * (plain text) or neither (the root AST node).
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCommentKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCommentKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXCommentInlineCommandRenderKind
template <class Pybind11T>
struct Bind_CXCommentInlineCommandRenderKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCommentInlineCommandRenderKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCommentInlineCommandRenderKind_Normal",
                   CXCommentInlineCommandRenderKind::
                       CXCommentInlineCommandRenderKind_Normal,
                   R"_pb11_weaver(/**
   * Command argument should be rendered in a normal font.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCommentInlineCommandRenderKind_Bold",
        CXCommentInlineCommandRenderKind::CXCommentInlineCommandRenderKind_Bold,
        R"_pb11_weaver(/**
   * Command argument should be rendered in a bold font.
   */)_pb11_weaver");
    pb11_obj.value("CXCommentInlineCommandRenderKind_Monospaced",
                   CXCommentInlineCommandRenderKind::
                       CXCommentInlineCommandRenderKind_Monospaced,
                   R"_pb11_weaver(/**
   * Command argument should be rendered in a monospaced font.
   */)_pb11_weaver");
    pb11_obj.value("CXCommentInlineCommandRenderKind_Emphasized",
                   CXCommentInlineCommandRenderKind::
                       CXCommentInlineCommandRenderKind_Emphasized,
                   R"_pb11_weaver(/**
   * Command argument should be rendered emphasized (typically italic
   * font).
   */)_pb11_weaver");
    pb11_obj.value("CXCommentInlineCommandRenderKind_Anchor",
                   CXCommentInlineCommandRenderKind::
                       CXCommentInlineCommandRenderKind_Anchor,
                   R"_pb11_weaver(/**
   * Command argument should not be rendered (since it only defines an anchor).
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCommentInlineCommandRenderKind"; }
};

struct Entity_CXCommentInlineCommandRenderKind
    : public Bind_CXCommentInlineCommandRenderKind<
          std::decay_t<pybind11::enum_<CXCommentInlineCommandRenderKind>>> {

  explicit Entity_CXCommentInlineCommandRenderKind(EntityScope parent_h)
      : Bind_CXCommentInlineCommandRenderKind<
            std::decay_t<pybind11::enum_<CXCommentInlineCommandRenderKind>>>(
            parent_h),
        handle{parent_h, "CXCommentInlineCommandRenderKind",
               pybind11::arithmetic(), R"_pb11_weaver(/**
 * The most appropriate rendering mode for an inline command, chosen on
 * command semantics in Doxygen.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCommentInlineCommandRenderKind> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCommentInlineCommandRenderKind

#ifndef PB11_WEAVER_DISABLE_Entity_CXCommentParamPassDirection
template <class Pybind11T>
struct Bind_CXCommentParamPassDirection : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_CXCommentParamPassDirection(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.value("CXCommentParamPassDirection_In",
                   CXCommentParamPassDirection::CXCommentParamPassDirection_In,
                   R"_pb11_weaver(/**
   * The parameter is an input parameter.
   */)_pb11_weaver");
    pb11_obj.value("CXCommentParamPassDirection_Out",
                   CXCommentParamPassDirection::CXCommentParamPassDirection_Out,
                   R"_pb11_weaver(/**
   * The parameter is an output parameter.
   */)_pb11_weaver");
    pb11_obj.value(
        "CXCommentParamPassDirection_InOut",
        CXCommentParamPassDirection::CXCommentParamPassDirection_InOut,
        R"_pb11_weaver(/**
   * The parameter is an input and output parameter.
   */)_pb11_weaver");
  }

  static const char *Key() { return "CXCommentParamPassDirection"; }
};

struct Entity_CXCommentParamPassDirection
    : public Bind_CXCommentParamPassDirection<
          std::decay_t<pybind11::enum_<CXCommentParamPassDirection>>> {

  explicit Entity_CXCommentParamPassDirection(EntityScope parent_h)
      : Bind_CXCommentParamPassDirection<
            std::decay_t<pybind11::enum_<CXCommentParamPassDirection>>>(
            parent_h),
        handle{parent_h, "CXCommentParamPassDirection", pybind11::arithmetic(),
               R"_pb11_weaver(/**
 * Describes parameter passing direction for \\param or \\arg command.
 */)_pb11_weaver"} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::enum_<CXCommentParamPassDirection> handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_CXCommentParamPassDirection

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Comment_getKind
template <class Pybind11T>
struct Bind_clang_Comment_getKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Comment_getKind(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Comment_getKind",
        static_cast<CXCommentKind (*)(CXComment)>(&clang_Comment_getKind),
        R"_pb11_weaver(/**
 * \param Comment AST node of any kind.
 *
 * \returns the type of the AST node.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Comment_getKind"; }
};

struct Entity_clang_Comment_getKind
    : public Bind_clang_Comment_getKind<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Comment_getKind(EntityScope parent_h)
      : Bind_clang_Comment_getKind<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Comment_getKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Comment_getNumChildren
template <class Pybind11T>
struct Bind_clang_Comment_getNumChildren : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Comment_getNumChildren(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Comment_getNumChildren",
        static_cast<unsigned int (*)(CXComment)>(&clang_Comment_getNumChildren),
        R"_pb11_weaver(/**
 * \param Comment AST node of any kind.
 *
 * \returns number of children of the AST node.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Comment_getNumChildren"; }
};

struct Entity_clang_Comment_getNumChildren
    : public Bind_clang_Comment_getNumChildren<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Comment_getNumChildren(EntityScope parent_h)
      : Bind_clang_Comment_getNumChildren<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Comment_getNumChildren

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Comment_getChild
template <class Pybind11T>
struct Bind_clang_Comment_getChild : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Comment_getChild(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_Comment_getChild",
                 static_cast<CXComment (*)(CXComment, unsigned int)>(
                     &clang_Comment_getChild),
                 R"_pb11_weaver(/**
 * \param Comment AST node of any kind.
 *
 * \param ChildIdx child index (zero-based).
 *
 * \returns the specified child of the AST node.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Comment_getChild"; }
};

struct Entity_clang_Comment_getChild
    : public Bind_clang_Comment_getChild<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Comment_getChild(EntityScope parent_h)
      : Bind_clang_Comment_getChild<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Comment_getChild

#ifndef PB11_WEAVER_DISABLE_Entity_clang_Comment_isWhitespace
template <class Pybind11T>
struct Bind_clang_Comment_isWhitespace : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_Comment_isWhitespace(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_Comment_isWhitespace",
        static_cast<unsigned int (*)(CXComment)>(&clang_Comment_isWhitespace),
        R"_pb11_weaver(/**
 * A \c CXComment_Paragraph node is considered whitespace if it contains
 * only \c CXComment_Text nodes that are empty or whitespace.
 *
 * Other AST nodes (except \c CXComment_Paragraph and \c CXComment_Text) are
 * never considered whitespace.
 *
 * \returns non-zero if \c Comment is whitespace.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_Comment_isWhitespace"; }
};

struct Entity_clang_Comment_isWhitespace
    : public Bind_clang_Comment_isWhitespace<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_Comment_isWhitespace(EntityScope parent_h)
      : Bind_clang_Comment_isWhitespace<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_Comment_isWhitespace

#ifndef PB11_WEAVER_DISABLE_Entity_clang_InlineContentComment_hasTrailingNewline
template <class Pybind11T>
struct Bind_clang_InlineContentComment_hasTrailingNewline : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_InlineContentComment_hasTrailingNewline(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_InlineContentComment_hasTrailingNewline",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_InlineContentComment_hasTrailingNewline),
                 R"_pb11_weaver(/**
 * \returns non-zero if \c Comment is inline content and has a newline
 * immediately following it in the comment text.  Newlines between paragraphs
 * do not count.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_InlineContentComment_hasTrailingNewline";
  }
};

struct Entity_clang_InlineContentComment_hasTrailingNewline
    : public Bind_clang_InlineContentComment_hasTrailingNewline<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_InlineContentComment_hasTrailingNewline(
      EntityScope parent_h)
      : Bind_clang_InlineContentComment_hasTrailingNewline<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_InlineContentComment_hasTrailingNewline

#ifndef PB11_WEAVER_DISABLE_Entity_clang_TextComment_getText
template <class Pybind11T>
struct Bind_clang_TextComment_getText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_TextComment_getText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_TextComment_getText",
        static_cast<CXString (*)(CXComment)>(&clang_TextComment_getText),
        R"_pb11_weaver(/**
 * \param Comment a \c CXComment_Text AST node.
 *
 * \returns text contained in the AST node.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_TextComment_getText"; }
};

struct Entity_clang_TextComment_getText
    : public Bind_clang_TextComment_getText<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_TextComment_getText(EntityScope parent_h)
      : Bind_clang_TextComment_getText<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_TextComment_getText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_InlineCommandComment_getCommandName
template <class Pybind11T>
struct Bind_clang_InlineCommandComment_getCommandName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_InlineCommandComment_getCommandName(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_InlineCommandComment_getCommandName",
                 static_cast<CXString (*)(CXComment)>(
                     &clang_InlineCommandComment_getCommandName),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_InlineCommand AST node.
 *
 * \returns name of the inline command.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_InlineCommandComment_getCommandName";
  }
};

struct Entity_clang_InlineCommandComment_getCommandName
    : public Bind_clang_InlineCommandComment_getCommandName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_InlineCommandComment_getCommandName(
      EntityScope parent_h)
      : Bind_clang_InlineCommandComment_getCommandName<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_InlineCommandComment_getCommandName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_InlineCommandComment_getRenderKind
template <class Pybind11T>
struct Bind_clang_InlineCommandComment_getRenderKind : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_InlineCommandComment_getRenderKind(EntityScope parent_h) {
  }

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_InlineCommandComment_getRenderKind",
                 static_cast<CXCommentInlineCommandRenderKind (*)(CXComment)>(
                     &clang_InlineCommandComment_getRenderKind),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_InlineCommand AST node.
 *
 * \returns the most appropriate rendering mode, chosen on command
 * semantics in Doxygen.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_InlineCommandComment_getRenderKind";
  }
};

struct Entity_clang_InlineCommandComment_getRenderKind
    : public Bind_clang_InlineCommandComment_getRenderKind<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_InlineCommandComment_getRenderKind(EntityScope parent_h)
      : Bind_clang_InlineCommandComment_getRenderKind<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_InlineCommandComment_getRenderKind

#ifndef PB11_WEAVER_DISABLE_Entity_clang_InlineCommandComment_getNumArgs
template <class Pybind11T>
struct Bind_clang_InlineCommandComment_getNumArgs : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_InlineCommandComment_getNumArgs(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_InlineCommandComment_getNumArgs",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_InlineCommandComment_getNumArgs),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_InlineCommand AST node.
 *
 * \returns number of command arguments.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_InlineCommandComment_getNumArgs"; }
};

struct Entity_clang_InlineCommandComment_getNumArgs
    : public Bind_clang_InlineCommandComment_getNumArgs<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_InlineCommandComment_getNumArgs(EntityScope parent_h)
      : Bind_clang_InlineCommandComment_getNumArgs<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_InlineCommandComment_getNumArgs

#ifndef PB11_WEAVER_DISABLE_Entity_clang_InlineCommandComment_getArgText
template <class Pybind11T>
struct Bind_clang_InlineCommandComment_getArgText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_InlineCommandComment_getArgText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_InlineCommandComment_getArgText",
                 static_cast<CXString (*)(CXComment, unsigned int)>(
                     &clang_InlineCommandComment_getArgText),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_InlineCommand AST node.
 *
 * \param ArgIdx argument index (zero-based).
 *
 * \returns text of the specified argument.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_InlineCommandComment_getArgText"; }
};

struct Entity_clang_InlineCommandComment_getArgText
    : public Bind_clang_InlineCommandComment_getArgText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_InlineCommandComment_getArgText(EntityScope parent_h)
      : Bind_clang_InlineCommandComment_getArgText<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_InlineCommandComment_getArgText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_HTMLTagComment_getTagName
template <class Pybind11T>
struct Bind_clang_HTMLTagComment_getTagName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_HTMLTagComment_getTagName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_HTMLTagComment_getTagName",
        static_cast<CXString (*)(CXComment)>(&clang_HTMLTagComment_getTagName),
        R"_pb11_weaver(/**
 * \param Comment a \c CXComment_HTMLStartTag or \c CXComment_HTMLEndTag AST
 * node.
 *
 * \returns HTML tag name.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_HTMLTagComment_getTagName"; }
};

struct Entity_clang_HTMLTagComment_getTagName
    : public Bind_clang_HTMLTagComment_getTagName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_HTMLTagComment_getTagName(EntityScope parent_h)
      : Bind_clang_HTMLTagComment_getTagName<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_HTMLTagComment_getTagName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_HTMLStartTagComment_isSelfClosing
template <class Pybind11T>
struct Bind_clang_HTMLStartTagComment_isSelfClosing : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_HTMLStartTagComment_isSelfClosing(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_HTMLStartTagComment_isSelfClosing",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_HTMLStartTagComment_isSelfClosing),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_HTMLStartTag AST node.
 *
 * \returns non-zero if tag is self-closing (for example, &lt;br /&gt;).
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_HTMLStartTagComment_isSelfClosing"; }
};

struct Entity_clang_HTMLStartTagComment_isSelfClosing
    : public Bind_clang_HTMLStartTagComment_isSelfClosing<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_HTMLStartTagComment_isSelfClosing(EntityScope parent_h)
      : Bind_clang_HTMLStartTagComment_isSelfClosing<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_HTMLStartTagComment_isSelfClosing

#ifndef PB11_WEAVER_DISABLE_Entity_clang_HTMLStartTag_getNumAttrs
template <class Pybind11T>
struct Bind_clang_HTMLStartTag_getNumAttrs : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_HTMLStartTag_getNumAttrs(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_HTMLStartTag_getNumAttrs",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_HTMLStartTag_getNumAttrs),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_HTMLStartTag AST node.
 *
 * \returns number of attributes (name-value pairs) attached to the start tag.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_HTMLStartTag_getNumAttrs"; }
};

struct Entity_clang_HTMLStartTag_getNumAttrs
    : public Bind_clang_HTMLStartTag_getNumAttrs<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_HTMLStartTag_getNumAttrs(EntityScope parent_h)
      : Bind_clang_HTMLStartTag_getNumAttrs<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_HTMLStartTag_getNumAttrs

#ifndef PB11_WEAVER_DISABLE_Entity_clang_HTMLStartTag_getAttrName
template <class Pybind11T>
struct Bind_clang_HTMLStartTag_getAttrName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_HTMLStartTag_getAttrName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_HTMLStartTag_getAttrName",
                 static_cast<CXString (*)(CXComment, unsigned int)>(
                     &clang_HTMLStartTag_getAttrName),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_HTMLStartTag AST node.
 *
 * \param AttrIdx attribute index (zero-based).
 *
 * \returns name of the specified attribute.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_HTMLStartTag_getAttrName"; }
};

struct Entity_clang_HTMLStartTag_getAttrName
    : public Bind_clang_HTMLStartTag_getAttrName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_HTMLStartTag_getAttrName(EntityScope parent_h)
      : Bind_clang_HTMLStartTag_getAttrName<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_HTMLStartTag_getAttrName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_HTMLStartTag_getAttrValue
template <class Pybind11T>
struct Bind_clang_HTMLStartTag_getAttrValue : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_HTMLStartTag_getAttrValue(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_HTMLStartTag_getAttrValue",
                 static_cast<CXString (*)(CXComment, unsigned int)>(
                     &clang_HTMLStartTag_getAttrValue),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_HTMLStartTag AST node.
 *
 * \param AttrIdx attribute index (zero-based).
 *
 * \returns value of the specified attribute.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_HTMLStartTag_getAttrValue"; }
};

struct Entity_clang_HTMLStartTag_getAttrValue
    : public Bind_clang_HTMLStartTag_getAttrValue<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_HTMLStartTag_getAttrValue(EntityScope parent_h)
      : Bind_clang_HTMLStartTag_getAttrValue<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_HTMLStartTag_getAttrValue

#ifndef PB11_WEAVER_DISABLE_Entity_clang_BlockCommandComment_getCommandName
template <class Pybind11T>
struct Bind_clang_BlockCommandComment_getCommandName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_BlockCommandComment_getCommandName(EntityScope parent_h) {
  }

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_BlockCommandComment_getCommandName",
                 static_cast<CXString (*)(CXComment)>(
                     &clang_BlockCommandComment_getCommandName),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_BlockCommand AST node.
 *
 * \returns name of the block command.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_BlockCommandComment_getCommandName";
  }
};

struct Entity_clang_BlockCommandComment_getCommandName
    : public Bind_clang_BlockCommandComment_getCommandName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_BlockCommandComment_getCommandName(EntityScope parent_h)
      : Bind_clang_BlockCommandComment_getCommandName<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_BlockCommandComment_getCommandName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_BlockCommandComment_getNumArgs
template <class Pybind11T>
struct Bind_clang_BlockCommandComment_getNumArgs : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_BlockCommandComment_getNumArgs(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_BlockCommandComment_getNumArgs",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_BlockCommandComment_getNumArgs),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_BlockCommand AST node.
 *
 * \returns number of word-like arguments.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_BlockCommandComment_getNumArgs"; }
};

struct Entity_clang_BlockCommandComment_getNumArgs
    : public Bind_clang_BlockCommandComment_getNumArgs<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_BlockCommandComment_getNumArgs(EntityScope parent_h)
      : Bind_clang_BlockCommandComment_getNumArgs<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_BlockCommandComment_getNumArgs

#ifndef PB11_WEAVER_DISABLE_Entity_clang_BlockCommandComment_getArgText
template <class Pybind11T>
struct Bind_clang_BlockCommandComment_getArgText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_BlockCommandComment_getArgText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_BlockCommandComment_getArgText",
                 static_cast<CXString (*)(CXComment, unsigned int)>(
                     &clang_BlockCommandComment_getArgText),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_BlockCommand AST node.
 *
 * \param ArgIdx argument index (zero-based).
 *
 * \returns text of the specified word-like argument.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_BlockCommandComment_getArgText"; }
};

struct Entity_clang_BlockCommandComment_getArgText
    : public Bind_clang_BlockCommandComment_getArgText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_BlockCommandComment_getArgText(EntityScope parent_h)
      : Bind_clang_BlockCommandComment_getArgText<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_BlockCommandComment_getArgText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_BlockCommandComment_getParagraph
template <class Pybind11T>
struct Bind_clang_BlockCommandComment_getParagraph : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_BlockCommandComment_getParagraph(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_BlockCommandComment_getParagraph",
                 static_cast<CXComment (*)(CXComment)>(
                     &clang_BlockCommandComment_getParagraph),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_BlockCommand or
 * \c CXComment_VerbatimBlockCommand AST node.
 *
 * \returns paragraph argument of the block command.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_BlockCommandComment_getParagraph"; }
};

struct Entity_clang_BlockCommandComment_getParagraph
    : public Bind_clang_BlockCommandComment_getParagraph<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_BlockCommandComment_getParagraph(EntityScope parent_h)
      : Bind_clang_BlockCommandComment_getParagraph<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_BlockCommandComment_getParagraph

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_getParamName
template <class Pybind11T>
struct Bind_clang_ParamCommandComment_getParamName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ParamCommandComment_getParamName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_ParamCommandComment_getParamName",
                 static_cast<CXString (*)(CXComment)>(
                     &clang_ParamCommandComment_getParamName),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns parameter name.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_ParamCommandComment_getParamName"; }
};

struct Entity_clang_ParamCommandComment_getParamName
    : public Bind_clang_ParamCommandComment_getParamName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ParamCommandComment_getParamName(EntityScope parent_h)
      : Bind_clang_ParamCommandComment_getParamName<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_getParamName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_isParamIndexValid
template <class Pybind11T>
struct Bind_clang_ParamCommandComment_isParamIndexValid : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ParamCommandComment_isParamIndexValid(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_ParamCommandComment_isParamIndexValid",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_ParamCommandComment_isParamIndexValid),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns non-zero if the parameter that this AST node represents was found
 * in the function prototype and \c clang_ParamCommandComment_getParamIndex
 * function will return a meaningful value.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_ParamCommandComment_isParamIndexValid";
  }
};

struct Entity_clang_ParamCommandComment_isParamIndexValid
    : public Bind_clang_ParamCommandComment_isParamIndexValid<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ParamCommandComment_isParamIndexValid(
      EntityScope parent_h)
      : Bind_clang_ParamCommandComment_isParamIndexValid<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_isParamIndexValid

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_getParamIndex
template <class Pybind11T>
struct Bind_clang_ParamCommandComment_getParamIndex : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ParamCommandComment_getParamIndex(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_ParamCommandComment_getParamIndex",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_ParamCommandComment_getParamIndex),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns zero-based parameter index in function prototype.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_ParamCommandComment_getParamIndex"; }
};

struct Entity_clang_ParamCommandComment_getParamIndex
    : public Bind_clang_ParamCommandComment_getParamIndex<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ParamCommandComment_getParamIndex(EntityScope parent_h)
      : Bind_clang_ParamCommandComment_getParamIndex<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_getParamIndex

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_isDirectionExplicit
template <class Pybind11T>
struct Bind_clang_ParamCommandComment_isDirectionExplicit : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ParamCommandComment_isDirectionExplicit(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_ParamCommandComment_isDirectionExplicit",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_ParamCommandComment_isDirectionExplicit),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns non-zero if parameter passing direction was specified explicitly in
 * the comment.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_ParamCommandComment_isDirectionExplicit";
  }
};

struct Entity_clang_ParamCommandComment_isDirectionExplicit
    : public Bind_clang_ParamCommandComment_isDirectionExplicit<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ParamCommandComment_isDirectionExplicit(
      EntityScope parent_h)
      : Bind_clang_ParamCommandComment_isDirectionExplicit<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_isDirectionExplicit

#ifndef PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_getDirection
template <class Pybind11T>
struct Bind_clang_ParamCommandComment_getDirection : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_ParamCommandComment_getDirection(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_ParamCommandComment_getDirection",
                 static_cast<CXCommentParamPassDirection (*)(CXComment)>(
                     &clang_ParamCommandComment_getDirection),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_ParamCommand AST node.
 *
 * \returns parameter passing direction.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_ParamCommandComment_getDirection"; }
};

struct Entity_clang_ParamCommandComment_getDirection
    : public Bind_clang_ParamCommandComment_getDirection<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_ParamCommandComment_getDirection(EntityScope parent_h)
      : Bind_clang_ParamCommandComment_getDirection<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_ParamCommandComment_getDirection

#ifndef PB11_WEAVER_DISABLE_Entity_clang_TParamCommandComment_getParamName
template <class Pybind11T>
struct Bind_clang_TParamCommandComment_getParamName : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_TParamCommandComment_getParamName(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_TParamCommandComment_getParamName",
                 static_cast<CXString (*)(CXComment)>(
                     &clang_TParamCommandComment_getParamName),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_TParamCommand AST node.
 *
 * \returns template parameter name.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_TParamCommandComment_getParamName"; }
};

struct Entity_clang_TParamCommandComment_getParamName
    : public Bind_clang_TParamCommandComment_getParamName<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_TParamCommandComment_getParamName(EntityScope parent_h)
      : Bind_clang_TParamCommandComment_getParamName<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_TParamCommandComment_getParamName

#ifndef PB11_WEAVER_DISABLE_Entity_clang_TParamCommandComment_isParamPositionValid
template <class Pybind11T>
struct Bind_clang_TParamCommandComment_isParamPositionValid
    : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_TParamCommandComment_isParamPositionValid(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_TParamCommandComment_isParamPositionValid",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_TParamCommandComment_isParamPositionValid),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_TParamCommand AST node.
 *
 * \returns non-zero if the parameter that this AST node represents was found
 * in the template parameter list and
 * \c clang_TParamCommandComment_getDepth and
 * \c clang_TParamCommandComment_getIndex functions will return a meaningful
 * value.
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_TParamCommandComment_isParamPositionValid";
  }
};

struct Entity_clang_TParamCommandComment_isParamPositionValid
    : public Bind_clang_TParamCommandComment_isParamPositionValid<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_TParamCommandComment_isParamPositionValid(
      EntityScope parent_h)
      : Bind_clang_TParamCommandComment_isParamPositionValid<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_TParamCommandComment_isParamPositionValid

#ifndef PB11_WEAVER_DISABLE_Entity_clang_TParamCommandComment_getDepth
template <class Pybind11T>
struct Bind_clang_TParamCommandComment_getDepth : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_TParamCommandComment_getDepth(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_TParamCommandComment_getDepth",
                 static_cast<unsigned int (*)(CXComment)>(
                     &clang_TParamCommandComment_getDepth),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_TParamCommand AST node.
 *
 * \returns zero-based nesting depth of this parameter in the template parameter list.
 *
 * For example,
 * \verbatim
 *     template<typename C, template<typename T> class TT>
 *     void test(TT<int> aaa);
 * \endverbatim
 * for C and TT nesting depth is 0,
 * for T nesting depth is 1.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_TParamCommandComment_getDepth"; }
};

struct Entity_clang_TParamCommandComment_getDepth
    : public Bind_clang_TParamCommandComment_getDepth<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_TParamCommandComment_getDepth(EntityScope parent_h)
      : Bind_clang_TParamCommandComment_getDepth<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_TParamCommandComment_getDepth

#ifndef PB11_WEAVER_DISABLE_Entity_clang_TParamCommandComment_getIndex
template <class Pybind11T>
struct Bind_clang_TParamCommandComment_getIndex : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_TParamCommandComment_getIndex(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_TParamCommandComment_getIndex",
                 static_cast<unsigned int (*)(CXComment, unsigned int)>(
                     &clang_TParamCommandComment_getIndex),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_TParamCommand AST node.
 *
 * \returns zero-based parameter index in the template parameter list at a
 * given nesting depth.
 *
 * For example,
 * \verbatim
 *     template<typename C, template<typename T> class TT>
 *     void test(TT<int> aaa);
 * \endverbatim
 * for C and TT nesting depth is 0, so we can ask for index at depth 0:
 * at depth 0 C's index is 0, TT's index is 1.
 *
 * For T nesting depth is 1, so we can ask for index at depth 0 and 1:
 * at depth 0 T's index is 1 (same as TT's),
 * at depth 1 T's index is 0.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_TParamCommandComment_getIndex"; }
};

struct Entity_clang_TParamCommandComment_getIndex
    : public Bind_clang_TParamCommandComment_getIndex<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_TParamCommandComment_getIndex(EntityScope parent_h)
      : Bind_clang_TParamCommandComment_getIndex<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_TParamCommandComment_getIndex

#ifndef PB11_WEAVER_DISABLE_Entity_clang_VerbatimBlockLineComment_getText
template <class Pybind11T>
struct Bind_clang_VerbatimBlockLineComment_getText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_VerbatimBlockLineComment_getText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_VerbatimBlockLineComment_getText",
                 static_cast<CXString (*)(CXComment)>(
                     &clang_VerbatimBlockLineComment_getText),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_VerbatimBlockLine AST node.
 *
 * \returns text contained in the AST node.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_VerbatimBlockLineComment_getText"; }
};

struct Entity_clang_VerbatimBlockLineComment_getText
    : public Bind_clang_VerbatimBlockLineComment_getText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_VerbatimBlockLineComment_getText(EntityScope parent_h)
      : Bind_clang_VerbatimBlockLineComment_getText<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_VerbatimBlockLineComment_getText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_VerbatimLineComment_getText
template <class Pybind11T>
struct Bind_clang_VerbatimLineComment_getText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_VerbatimLineComment_getText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_VerbatimLineComment_getText",
                 static_cast<CXString (*)(CXComment)>(
                     &clang_VerbatimLineComment_getText),
                 R"_pb11_weaver(/**
 * \param Comment a \c CXComment_VerbatimLine AST node.
 *
 * \returns text contained in the AST node.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_VerbatimLineComment_getText"; }
};

struct Entity_clang_VerbatimLineComment_getText
    : public Bind_clang_VerbatimLineComment_getText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_VerbatimLineComment_getText(EntityScope parent_h)
      : Bind_clang_VerbatimLineComment_getText<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_VerbatimLineComment_getText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_HTMLTagComment_getAsString
template <class Pybind11T>
struct Bind_clang_HTMLTagComment_getAsString : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_HTMLTagComment_getAsString(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_HTMLTagComment_getAsString",
        static_cast<CXString (*)(CXComment)>(&clang_HTMLTagComment_getAsString),
        R"_pb11_weaver(/**
 * Convert an HTML tag AST node to string.
 *
 * \param Comment a \c CXComment_HTMLStartTag or \c CXComment_HTMLEndTag AST
 * node.
 *
 * \returns string containing an HTML tag.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_HTMLTagComment_getAsString"; }
};

struct Entity_clang_HTMLTagComment_getAsString
    : public Bind_clang_HTMLTagComment_getAsString<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_HTMLTagComment_getAsString(EntityScope parent_h)
      : Bind_clang_HTMLTagComment_getAsString<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_HTMLTagComment_getAsString

#ifndef PB11_WEAVER_DISABLE_Entity_clang_FullComment_getAsHTML
template <class Pybind11T>
struct Bind_clang_FullComment_getAsHTML : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_FullComment_getAsHTML(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_FullComment_getAsHTML",
        static_cast<CXString (*)(CXComment)>(&clang_FullComment_getAsHTML),
        R"_pb11_weaver(/**
 * Convert a given full parsed comment to an HTML fragment.
 *
 * Specific details of HTML layout are subject to change.  Don't try to parse
 * this HTML back into an AST, use other APIs instead.
 *
 * Currently the following CSS classes are used:
 * \li "para-brief" for \paragraph and equivalent commands;
 * \li "para-returns" for \\returns paragraph and equivalent commands;
 * \li "word-returns" for the "Returns" word in \\returns paragraph.
 *
 * Function argument documentation is rendered as a \<dl\> list with arguments
 * sorted in function prototype order.  CSS classes used:
 * \li "param-name-index-NUMBER" for parameter name (\<dt\>);
 * \li "param-descr-index-NUMBER" for parameter description (\<dd\>);
 * \li "param-name-index-invalid" and "param-descr-index-invalid" are used if
 * parameter index is invalid.
 *
 * Template parameter documentation is rendered as a \<dl\> list with
 * parameters sorted in template parameter list order.  CSS classes used:
 * \li "tparam-name-index-NUMBER" for parameter name (\<dt\>);
 * \li "tparam-descr-index-NUMBER" for parameter description (\<dd\>);
 * \li "tparam-name-index-other" and "tparam-descr-index-other" are used for
 * names inside template template parameters;
 * \li "tparam-name-index-invalid" and "tparam-descr-index-invalid" are used if
 * parameter position is invalid.
 *
 * \param Comment a \c CXComment_FullComment AST node.
 *
 * \returns string containing an HTML fragment.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_FullComment_getAsHTML"; }
};

struct Entity_clang_FullComment_getAsHTML
    : public Bind_clang_FullComment_getAsHTML<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_FullComment_getAsHTML(EntityScope parent_h)
      : Bind_clang_FullComment_getAsHTML<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_FullComment_getAsHTML

#ifndef PB11_WEAVER_DISABLE_Entity_clang_FullComment_getAsXML
template <class Pybind11T>
struct Bind_clang_FullComment_getAsXML : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_FullComment_getAsXML(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_FullComment_getAsXML",
        static_cast<CXString (*)(CXComment)>(&clang_FullComment_getAsXML),
        R"_pb11_weaver(/**
 * Convert a given full parsed comment to an XML document.
 *
 * A Relax NG schema for the XML can be found in comment-xml-schema.rng file
 * inside clang source tree.
 *
 * \param Comment a \c CXComment_FullComment AST node.
 *
 * \returns string containing an XML document.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_FullComment_getAsXML"; }
};

struct Entity_clang_FullComment_getAsXML
    : public Bind_clang_FullComment_getAsXML<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_FullComment_getAsXML(EntityScope parent_h)
      : Bind_clang_FullComment_getAsXML<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_FullComment_getAsXML

#ifndef PB11_WEAVER_DISABLE_Entity_clang_createAPISet
template <class Pybind11T> struct Bind_clang_createAPISet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_createAPISet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_createAPISet",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *tu,
            pybind11_weaver::PointerWrapper<CXAPISetImpl **> *out_api) {
          return clang_createAPISet((tu)->ptr, (out_api)->ptr);
        },
        R"_pb11_weaver(/**
 * Traverses the translation unit to create a \c CXAPISet.
 *
 * \param tu is the \c CXTranslationUnit to build the \c CXAPISet for.
 *
 * \param out_api is a pointer to the output of this function. It is needs to be
 * disposed of by calling clang_disposeAPISet.
 *
 * \returns Error code indicating success or failure of the APISet creation.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_createAPISet"; }
};

struct Entity_clang_createAPISet
    : public Bind_clang_createAPISet<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_createAPISet(EntityScope parent_h)
      : Bind_clang_createAPISet<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_createAPISet

#ifndef PB11_WEAVER_DISABLE_Entity_clang_disposeAPISet
template <class Pybind11T> struct Bind_clang_disposeAPISet : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_disposeAPISet(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_disposeAPISet",
        [=](pybind11_weaver::PointerWrapper<CXAPISetImpl *> *api) {
          return clang_disposeAPISet((api)->ptr);
        },
        R"_pb11_weaver(/**
 * Dispose of an APISet.
 *
 * The provided \c CXAPISet can not be used after this function is called.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_disposeAPISet"; }
};

struct Entity_clang_disposeAPISet
    : public Bind_clang_disposeAPISet<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_disposeAPISet(EntityScope parent_h)
      : Bind_clang_disposeAPISet<std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_disposeAPISet

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getSymbolGraphForUSR
template <class Pybind11T>
struct Bind_clang_getSymbolGraphForUSR : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getSymbolGraphForUSR(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getSymbolGraphForUSR",
        [=](const char *usr,
            pybind11_weaver::PointerWrapper<CXAPISetImpl *> *api) {
          return clang_getSymbolGraphForUSR(usr, (api)->ptr);
        },
        R"_pb11_weaver(/**
 * Generate a single symbol symbol graph for the given USR. Returns a null
 * string if the associated symbol can not be found in the provided \c CXAPISet.
 *
 * The output contains the symbol graph as well as some additional information
 * about related symbols.
 *
 * \param usr is a string containing the USR of the symbol to generate the
 * symbol graph for.
 *
 * \param api the \c CXAPISet to look for the symbol in.
 *
 * \returns a string containing the serialized symbol graph representation for
 * the symbol being queried or a null string if it can not be found in the
 * APISet.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getSymbolGraphForUSR"; }
};

struct Entity_clang_getSymbolGraphForUSR
    : public Bind_clang_getSymbolGraphForUSR<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getSymbolGraphForUSR(EntityScope parent_h)
      : Bind_clang_getSymbolGraphForUSR<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getSymbolGraphForUSR

#ifndef PB11_WEAVER_DISABLE_Entity_clang_getSymbolGraphForCursor
template <class Pybind11T>
struct Bind_clang_getSymbolGraphForCursor : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_getSymbolGraphForCursor(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_getSymbolGraphForCursor",
        static_cast<CXString (*)(CXCursor)>(&clang_getSymbolGraphForCursor),
        R"_pb11_weaver(/**
 * Generate a single symbol symbol graph for the declaration at the given
 * cursor. Returns a null string if the AST node for the cursor isn't a
 * declaration.
 *
 * The output contains the symbol graph as well as some additional information
 * about related symbols.
 *
 * \param cursor the declaration for which to generate the single symbol symbol
 * graph.
 *
 * \returns a string containing the serialized symbol graph representation for
 * the symbol being queried or a null string if it can not be found in the
 * APISet.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_getSymbolGraphForCursor"; }
};

struct Entity_clang_getSymbolGraphForCursor
    : public Bind_clang_getSymbolGraphForCursor<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_getSymbolGraphForCursor(EntityScope parent_h)
      : Bind_clang_getSymbolGraphForCursor<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_getSymbolGraphForCursor

#ifndef PB11_WEAVER_DISABLE_Entity_clang_install_aborting_llvm_fatal_error_handler
template <class Pybind11T>
struct Bind_clang_install_aborting_llvm_fatal_error_handler
    : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_install_aborting_llvm_fatal_error_handler(
      EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def("clang_install_aborting_llvm_fatal_error_handler",
                 static_cast<void (*)()>(
                     &clang_install_aborting_llvm_fatal_error_handler),
                 R"_pb11_weaver(/**
 * Installs error handler that prints error message to stderr and calls abort().
 * Replaces currently installed error handler (if any).
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_install_aborting_llvm_fatal_error_handler";
  }
};

struct Entity_clang_install_aborting_llvm_fatal_error_handler
    : public Bind_clang_install_aborting_llvm_fatal_error_handler<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_install_aborting_llvm_fatal_error_handler(
      EntityScope parent_h)
      : Bind_clang_install_aborting_llvm_fatal_error_handler<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_install_aborting_llvm_fatal_error_handler

#ifndef PB11_WEAVER_DISABLE_Entity_clang_uninstall_llvm_fatal_error_handler
template <class Pybind11T>
struct Bind_clang_uninstall_llvm_fatal_error_handler : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_uninstall_llvm_fatal_error_handler(EntityScope parent_h) {
  }

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_uninstall_llvm_fatal_error_handler",
        static_cast<void (*)()>(&clang_uninstall_llvm_fatal_error_handler),
        R"_pb11_weaver(/**
 * Removes currently installed error handler (if any).
 * If no error handler is intalled, the default strategy is to print error
 * message to stderr and call exit(1).
 */)_pb11_weaver");
  }

  static const char *Key() {
    return "clang_uninstall_llvm_fatal_error_handler";
  }
};

struct Entity_clang_uninstall_llvm_fatal_error_handler
    : public Bind_clang_uninstall_llvm_fatal_error_handler<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_uninstall_llvm_fatal_error_handler(EntityScope parent_h)
      : Bind_clang_uninstall_llvm_fatal_error_handler<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_uninstall_llvm_fatal_error_handler

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_create
template <class Pybind11T>
struct Bind_clang_CXRewriter_create : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXRewriter_create(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXRewriter_create",
        [=](pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *> *TU) {
          return pybind11_weaver::WrapP<void *>(
                     clang_CXRewriter_create((TU)->ptr))
              .release();
        },
        R"_pb11_weaver(/**
 * Create CXRewriter.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXRewriter_create"; }
};

struct Entity_clang_CXRewriter_create
    : public Bind_clang_CXRewriter_create<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXRewriter_create(EntityScope parent_h)
      : Bind_clang_CXRewriter_create<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_create

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_insertTextBefore
template <class Pybind11T>
struct Bind_clang_CXRewriter_insertTextBefore : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXRewriter_insertTextBefore(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXRewriter_insertTextBefore",
        [=](pybind11_weaver::PointerWrapper<void *> *Rew, CXSourceLocation Loc,
            const char *Insert) {
          return clang_CXRewriter_insertTextBefore((Rew)->ptr, Loc, Insert);
        },
        R"_pb11_weaver(/**
 * Insert the specified string at the specified location in the original buffer.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXRewriter_insertTextBefore"; }
};

struct Entity_clang_CXRewriter_insertTextBefore
    : public Bind_clang_CXRewriter_insertTextBefore<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXRewriter_insertTextBefore(EntityScope parent_h)
      : Bind_clang_CXRewriter_insertTextBefore<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_insertTextBefore

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_replaceText
template <class Pybind11T>
struct Bind_clang_CXRewriter_replaceText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXRewriter_replaceText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXRewriter_replaceText",
        [=](pybind11_weaver::PointerWrapper<void *> *Rew,
            CXSourceRange ToBeReplaced, const char *Replacement) {
          return clang_CXRewriter_replaceText((Rew)->ptr, ToBeReplaced,
                                              Replacement);
        },
        R"_pb11_weaver(/**
 * Replace the specified range of characters in the input with the specified
 * replacement.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXRewriter_replaceText"; }
};

struct Entity_clang_CXRewriter_replaceText
    : public Bind_clang_CXRewriter_replaceText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXRewriter_replaceText(EntityScope parent_h)
      : Bind_clang_CXRewriter_replaceText<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_replaceText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_removeText
template <class Pybind11T>
struct Bind_clang_CXRewriter_removeText : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXRewriter_removeText(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXRewriter_removeText",
        [=](pybind11_weaver::PointerWrapper<void *> *Rew,
            CXSourceRange ToBeRemoved) {
          return clang_CXRewriter_removeText((Rew)->ptr, ToBeRemoved);
        },
        R"_pb11_weaver(/**
 * Remove the specified range.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXRewriter_removeText"; }
};

struct Entity_clang_CXRewriter_removeText
    : public Bind_clang_CXRewriter_removeText<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXRewriter_removeText(EntityScope parent_h)
      : Bind_clang_CXRewriter_removeText<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_removeText

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_overwriteChangedFiles
template <class Pybind11T>
struct Bind_clang_CXRewriter_overwriteChangedFiles : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXRewriter_overwriteChangedFiles(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXRewriter_overwriteChangedFiles",
        [=](pybind11_weaver::PointerWrapper<void *> *Rew) {
          return clang_CXRewriter_overwriteChangedFiles((Rew)->ptr);
        },
        R"_pb11_weaver(/**
 * Save all changed files to disk.
 * Returns 1 if any files were not saved successfully, returns 0 otherwise.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXRewriter_overwriteChangedFiles"; }
};

struct Entity_clang_CXRewriter_overwriteChangedFiles
    : public Bind_clang_CXRewriter_overwriteChangedFiles<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXRewriter_overwriteChangedFiles(EntityScope parent_h)
      : Bind_clang_CXRewriter_overwriteChangedFiles<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_overwriteChangedFiles

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_writeMainFileToStdOut
template <class Pybind11T>
struct Bind_clang_CXRewriter_writeMainFileToStdOut : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXRewriter_writeMainFileToStdOut(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXRewriter_writeMainFileToStdOut",
        [=](pybind11_weaver::PointerWrapper<void *> *Rew) {
          return clang_CXRewriter_writeMainFileToStdOut((Rew)->ptr);
        },
        R"_pb11_weaver(/**
 * Write out rewritten version of the main file to stdout.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXRewriter_writeMainFileToStdOut"; }
};

struct Entity_clang_CXRewriter_writeMainFileToStdOut
    : public Bind_clang_CXRewriter_writeMainFileToStdOut<
          std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXRewriter_writeMainFileToStdOut(EntityScope parent_h)
      : Bind_clang_CXRewriter_writeMainFileToStdOut<
            std::decay_t<pybind11::module_ &>>(parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_writeMainFileToStdOut

#ifndef PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_dispose
template <class Pybind11T>
struct Bind_clang_CXRewriter_dispose : public EntityBase {
  using Pybind11Type = Pybind11T;

  explicit Bind_clang_CXRewriter_dispose(EntityScope parent_h) {}

  virtual void Bind(Pybind11T &pb11_obj) {
    pb11_obj.def(
        "clang_CXRewriter_dispose",
        [=](pybind11_weaver::PointerWrapper<void *> *Rew) {
          return clang_CXRewriter_dispose((Rew)->ptr);
        },
        R"_pb11_weaver(/**
 * Free the given CXRewriter.
 */)_pb11_weaver");
  }

  static const char *Key() { return "clang_CXRewriter_dispose"; }
};

struct Entity_clang_CXRewriter_dispose
    : public Bind_clang_CXRewriter_dispose<std::decay_t<pybind11::module_ &>> {

  explicit Entity_clang_CXRewriter_dispose(EntityScope parent_h)
      : Bind_clang_CXRewriter_dispose<std::decay_t<pybind11::module_ &>>(
            parent_h),
        handle{static_cast<pybind11::module_ &>(parent_h)} {}

  void Update() override { Bind(handle); }

  EntityScope AsScope() override { return EntityScope(handle); }

  pybind11::module_ &handle;
};
#endif // PB11_WEAVER_DISABLE_Entity_clang_CXRewriter_dispose

/**
 * Create all entities, return a callable guard that can be called to update all
 *entities. If the returned guard is not called, the guard will call the update
 *function on its destruction.
 **/
[[nodiscard]] pybind11_weaver::CallUpdateGuard
DeclFn(pybind11::module &m,
       const pybind11_weaver::CustomBindingRegistry &registry) {
  pybind11_weaver::PointerWrapper<CXAPISetImpl *>::FastBind(m, "CXAPISetImplp");
  pybind11_weaver::PointerWrapper<CXAPISetImpl **>::FastBind(m,
                                                             "CXAPISetImplpp");
  pybind11_weaver::PointerWrapper<CXCursor **>::FastBind(m, "CXCursorpp");
  pybind11_weaver::PointerWrapper<CXCursorSetImpl *>::FastBind(
      m, "CXCursorSetImplp");
  pybind11_weaver::PointerWrapper<CXModuleMapDescriptorImpl *>::FastBind(
      m, "CXModuleMapDescriptorImplp");
  pybind11_weaver::PointerWrapper<CXTargetInfoImpl *>::FastBind(
      m, "CXTargetInfoImplp");
  pybind11_weaver::PointerWrapper<CXToken **>::FastBind(m, "CXTokenpp");
  pybind11_weaver::PointerWrapper<CXTranslationUnitImpl *>::FastBind(
      m, "CXTranslationUnitImplp");
  pybind11_weaver::PointerWrapper<CXTranslationUnitImpl **>::FastBind(
      m, "CXTranslationUnitImplpp");
  pybind11_weaver::PointerWrapper<CXVirtualFileOverlayImpl *>::FastBind(
      m, "CXVirtualFileOverlayImplp");
  pybind11_weaver::PointerWrapper<char **>::FastBind(m, "charpp");
  pybind11_weaver::PointerWrapper<const CXIdxAttrInfo *const *>::FastBind(
      m, "constCXIdxAttrInfopconstp");
  pybind11_weaver::PointerWrapper<const CXIdxBaseClassInfo *const *>::FastBind(
      m, "constCXIdxBaseClassInfopconstp");
  pybind11_weaver::PointerWrapper<const CXIdxObjCProtocolRefInfo *const *>::
      FastBind(m, "constCXIdxObjCProtocolRefInfopconstp");
  pybind11_weaver::PointerWrapper<const char **>::FastBind(m, "constcharpp");
  pybind11_weaver::PointerWrapper<const char *const *>::FastBind(
      m, "constcharpconstp");
  pybind11_weaver::PointerWrapper<const void *>::FastBind(m, "constvoidp");
  pybind11_weaver::PointerWrapper<void *>::FastBind(m, "voidp");
  pybind11_weaver::PointerWrapper<void **>::FastBind(m, "voidpp");

  auto v0 = pybind11_weaver::CreateEntity<Entity_CXErrorCode>(EntityScope(m),
                                                              registry);
  auto v1 =
      pybind11_weaver::CreateEntity<Entity_CXString>(EntityScope(m), registry);
  auto v2 = pybind11_weaver::CreateEntity<Entity_CXStringSet>(EntityScope(m),
                                                              registry);
  auto v3 = pybind11_weaver::CreateEntity<Entity_clang_getCString>(
      EntityScope(m), registry);
  auto v4 = pybind11_weaver::CreateEntity<Entity_clang_disposeString>(
      EntityScope(m), registry);
  auto v5 = pybind11_weaver::CreateEntity<Entity_clang_disposeStringSet>(
      EntityScope(m), registry);
  auto v6 =
      pybind11_weaver::CreateEntity<Entity_clang_getBuildSessionTimestamp>(
          EntityScope(m), registry);
  auto v7 =
      pybind11_weaver::CreateEntity<Entity_clang_VirtualFileOverlay_create>(
          EntityScope(m), registry);
  auto v8 = pybind11_weaver::CreateEntity<
      Entity_clang_VirtualFileOverlay_addFileMapping>(EntityScope(m), registry);
  auto v9 = pybind11_weaver::CreateEntity<
      Entity_clang_VirtualFileOverlay_setCaseSensitivity>(EntityScope(m),
                                                          registry);
  auto v10 = pybind11_weaver::CreateEntity<
      Entity_clang_VirtualFileOverlay_writeToBuffer>(EntityScope(m), registry);
  auto v11 = pybind11_weaver::CreateEntity<Entity_clang_free>(EntityScope(m),
                                                              registry);
  auto v12 =
      pybind11_weaver::CreateEntity<Entity_clang_VirtualFileOverlay_dispose>(
          EntityScope(m), registry);
  auto v13 =
      pybind11_weaver::CreateEntity<Entity_clang_ModuleMapDescriptor_create>(
          EntityScope(m), registry);
  auto v14 = pybind11_weaver::CreateEntity<
      Entity_clang_ModuleMapDescriptor_setFrameworkModuleName>(EntityScope(m),
                                                               registry);
  auto v15 = pybind11_weaver::CreateEntity<
      Entity_clang_ModuleMapDescriptor_setUmbrellaHeader>(EntityScope(m),
                                                          registry);
  auto v16 = pybind11_weaver::CreateEntity<
      Entity_clang_ModuleMapDescriptor_writeToBuffer>(EntityScope(m), registry);
  auto v17 =
      pybind11_weaver::CreateEntity<Entity_clang_ModuleMapDescriptor_dispose>(
          EntityScope(m), registry);
  auto v18 = pybind11_weaver::CreateEntity<Entity_CXCompilationDatabase_Error>(
      EntityScope(m), registry);
  auto v19 = pybind11_weaver::CreateEntity<
      Entity_clang_CompilationDatabase_fromDirectory>(EntityScope(m), registry);
  auto v20 =
      pybind11_weaver::CreateEntity<Entity_clang_CompilationDatabase_dispose>(
          EntityScope(m), registry);
  auto v21 = pybind11_weaver::CreateEntity<
      Entity_clang_CompilationDatabase_getCompileCommands>(EntityScope(m),
                                                           registry);
  auto v22 = pybind11_weaver::CreateEntity<
      Entity_clang_CompilationDatabase_getAllCompileCommands>(EntityScope(m),
                                                              registry);
  auto v23 =
      pybind11_weaver::CreateEntity<Entity_clang_CompileCommands_dispose>(
          EntityScope(m), registry);
  auto v24 =
      pybind11_weaver::CreateEntity<Entity_clang_CompileCommands_getSize>(
          EntityScope(m), registry);
  auto v25 =
      pybind11_weaver::CreateEntity<Entity_clang_CompileCommands_getCommand>(
          EntityScope(m), registry);
  auto v26 =
      pybind11_weaver::CreateEntity<Entity_clang_CompileCommand_getDirectory>(
          EntityScope(m), registry);
  auto v27 =
      pybind11_weaver::CreateEntity<Entity_clang_CompileCommand_getFilename>(
          EntityScope(m), registry);
  auto v28 =
      pybind11_weaver::CreateEntity<Entity_clang_CompileCommand_getNumArgs>(
          EntityScope(m), registry);
  auto v29 = pybind11_weaver::CreateEntity<Entity_clang_CompileCommand_getArg>(
      EntityScope(m), registry);
  auto v30 = pybind11_weaver::CreateEntity<
      Entity_clang_CompileCommand_getNumMappedSources>(EntityScope(m),
                                                       registry);
  auto v31 = pybind11_weaver::CreateEntity<
      Entity_clang_CompileCommand_getMappedSourcePath>(EntityScope(m),
                                                       registry);
  auto v32 = pybind11_weaver::CreateEntity<
      Entity_clang_CompileCommand_getMappedSourceContent>(EntityScope(m),
                                                          registry);
  auto v33 = pybind11_weaver::CreateEntity<Entity_clang_getFileName>(
      EntityScope(m), registry);
  auto v34 = pybind11_weaver::CreateEntity<Entity_clang_getFileTime>(
      EntityScope(m), registry);
  auto v35 = pybind11_weaver::CreateEntity<Entity_CXFileUniqueID>(
      EntityScope(m), registry);
  auto v36 = pybind11_weaver::CreateEntity<Entity_clang_getFileUniqueID>(
      EntityScope(m), registry);
  auto v37 = pybind11_weaver::CreateEntity<Entity_clang_File_isEqual>(
      EntityScope(m), registry);
  auto v38 =
      pybind11_weaver::CreateEntity<Entity_clang_File_tryGetRealPathName>(
          EntityScope(m), registry);
  auto v39 = pybind11_weaver::CreateEntity<Entity_CXSourceLocation>(
      EntityScope(m), registry);
  auto v40 = pybind11_weaver::CreateEntity<Entity_CXSourceRange>(EntityScope(m),
                                                                 registry);
  auto v41 = pybind11_weaver::CreateEntity<Entity_clang_getNullLocation>(
      EntityScope(m), registry);
  auto v42 = pybind11_weaver::CreateEntity<Entity_clang_equalLocations>(
      EntityScope(m), registry);
  auto v43 =
      pybind11_weaver::CreateEntity<Entity_clang_Location_isInSystemHeader>(
          EntityScope(m), registry);
  auto v44 =
      pybind11_weaver::CreateEntity<Entity_clang_Location_isFromMainFile>(
          EntityScope(m), registry);
  auto v45 = pybind11_weaver::CreateEntity<Entity_clang_getNullRange>(
      EntityScope(m), registry);
  auto v46 = pybind11_weaver::CreateEntity<Entity_clang_getRange>(
      EntityScope(m), registry);
  auto v47 = pybind11_weaver::CreateEntity<Entity_clang_equalRanges>(
      EntityScope(m), registry);
  auto v48 = pybind11_weaver::CreateEntity<Entity_clang_Range_isNull>(
      EntityScope(m), registry);
  auto v49 = pybind11_weaver::CreateEntity<Entity_clang_getExpansionLocation>(
      EntityScope(m), registry);
  auto v50 = pybind11_weaver::CreateEntity<Entity_clang_getPresumedLocation>(
      EntityScope(m), registry);
  auto v51 =
      pybind11_weaver::CreateEntity<Entity_clang_getInstantiationLocation>(
          EntityScope(m), registry);
  auto v52 = pybind11_weaver::CreateEntity<Entity_clang_getSpellingLocation>(
      EntityScope(m), registry);
  auto v53 = pybind11_weaver::CreateEntity<Entity_clang_getFileLocation>(
      EntityScope(m), registry);
  auto v54 = pybind11_weaver::CreateEntity<Entity_clang_getRangeStart>(
      EntityScope(m), registry);
  auto v55 = pybind11_weaver::CreateEntity<Entity_clang_getRangeEnd>(
      EntityScope(m), registry);
  auto v56 = pybind11_weaver::CreateEntity<Entity_CXSourceRangeList>(
      EntityScope(m), registry);
  auto v57 = pybind11_weaver::CreateEntity<Entity_clang_disposeSourceRangeList>(
      EntityScope(m), registry);
  auto v58 = pybind11_weaver::CreateEntity<Entity_CXDiagnosticSeverity>(
      EntityScope(m), registry);
  auto v59 = pybind11_weaver::CreateEntity<Entity_clang_getNumDiagnosticsInSet>(
      EntityScope(m), registry);
  auto v60 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticInSet>(
      EntityScope(m), registry);
  auto v61 = pybind11_weaver::CreateEntity<Entity_CXLoadDiag_Error>(
      EntityScope(m), registry);
  auto v62 = pybind11_weaver::CreateEntity<Entity_clang_loadDiagnostics>(
      EntityScope(m), registry);
  auto v63 = pybind11_weaver::CreateEntity<Entity_clang_disposeDiagnosticSet>(
      EntityScope(m), registry);
  auto v64 = pybind11_weaver::CreateEntity<Entity_clang_getChildDiagnostics>(
      EntityScope(m), registry);
  auto v65 = pybind11_weaver::CreateEntity<Entity_clang_disposeDiagnostic>(
      EntityScope(m), registry);
  auto v66 = pybind11_weaver::CreateEntity<Entity_CXDiagnosticDisplayOptions>(
      EntityScope(m), registry);
  auto v67 = pybind11_weaver::CreateEntity<Entity_clang_formatDiagnostic>(
      EntityScope(m), registry);
  auto v68 = pybind11_weaver::CreateEntity<
      Entity_clang_defaultDiagnosticDisplayOptions>(EntityScope(m), registry);
  auto v69 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticSeverity>(
      EntityScope(m), registry);
  auto v70 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticLocation>(
      EntityScope(m), registry);
  auto v71 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticSpelling>(
      EntityScope(m), registry);
  auto v72 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticOption>(
      EntityScope(m), registry);
  auto v73 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticCategory>(
      EntityScope(m), registry);
  auto v74 =
      pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticCategoryName>(
          EntityScope(m), registry);
  auto v75 =
      pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticCategoryText>(
          EntityScope(m), registry);
  auto v76 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticNumRanges>(
      EntityScope(m), registry);
  auto v77 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticRange>(
      EntityScope(m), registry);
  auto v78 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticNumFixIts>(
      EntityScope(m), registry);
  auto v79 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticFixIt>(
      EntityScope(m), registry);
  auto v80 = pybind11_weaver::CreateEntity<Entity_CXUnsavedFile>(EntityScope(m),
                                                                 registry);
  auto v81 = pybind11_weaver::CreateEntity<Entity_CXAvailabilityKind>(
      EntityScope(m), registry);
  auto v82 =
      pybind11_weaver::CreateEntity<Entity_CXVersion>(EntityScope(m), registry);
  auto v83 =
      pybind11_weaver::CreateEntity<Entity_CXCursor_ExceptionSpecificationKind>(
          EntityScope(m), registry);
  auto v84 = pybind11_weaver::CreateEntity<Entity_clang_createIndex>(
      EntityScope(m), registry);
  auto v85 = pybind11_weaver::CreateEntity<Entity_clang_disposeIndex>(
      EntityScope(m), registry);
  auto v86 = pybind11_weaver::CreateEntity<Entity_CXGlobalOptFlags>(
      EntityScope(m), registry);
  auto v87 =
      pybind11_weaver::CreateEntity<Entity_clang_CXIndex_setGlobalOptions>(
          EntityScope(m), registry);
  auto v88 =
      pybind11_weaver::CreateEntity<Entity_clang_CXIndex_getGlobalOptions>(
          EntityScope(m), registry);
  auto v89 = pybind11_weaver::CreateEntity<
      Entity_clang_CXIndex_setInvocationEmissionPathOption>(EntityScope(m),
                                                            registry);
  auto v90 =
      pybind11_weaver::CreateEntity<Entity_clang_isFileMultipleIncludeGuarded>(
          EntityScope(m), registry);
  auto v91 = pybind11_weaver::CreateEntity<Entity_clang_getFile>(EntityScope(m),
                                                                 registry);
  auto v92 = pybind11_weaver::CreateEntity<Entity_clang_getFileContents>(
      EntityScope(m), registry);
  auto v93 = pybind11_weaver::CreateEntity<Entity_clang_getLocation>(
      EntityScope(m), registry);
  auto v94 = pybind11_weaver::CreateEntity<Entity_clang_getLocationForOffset>(
      EntityScope(m), registry);
  auto v95 = pybind11_weaver::CreateEntity<Entity_clang_getSkippedRanges>(
      EntityScope(m), registry);
  auto v96 = pybind11_weaver::CreateEntity<Entity_clang_getAllSkippedRanges>(
      EntityScope(m), registry);
  auto v97 = pybind11_weaver::CreateEntity<Entity_clang_getNumDiagnostics>(
      EntityScope(m), registry);
  auto v98 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnostic>(
      EntityScope(m), registry);
  auto v99 = pybind11_weaver::CreateEntity<Entity_clang_getDiagnosticSetFromTU>(
      EntityScope(m), registry);
  auto v100 =
      pybind11_weaver::CreateEntity<Entity_clang_getTranslationUnitSpelling>(
          EntityScope(m), registry);
  auto v101 = pybind11_weaver::CreateEntity<
      Entity_clang_createTranslationUnitFromSourceFile>(EntityScope(m),
                                                        registry);
  auto v102 = pybind11_weaver::CreateEntity<Entity_clang_createTranslationUnit>(
      EntityScope(m), registry);
  auto v103 =
      pybind11_weaver::CreateEntity<Entity_clang_createTranslationUnit2>(
          EntityScope(m), registry);
  auto v104 = pybind11_weaver::CreateEntity<Entity_CXTranslationUnit_Flags>(
      EntityScope(m), registry);
  auto v105 = pybind11_weaver::CreateEntity<
      Entity_clang_defaultEditingTranslationUnitOptions>(EntityScope(m),
                                                         registry);
  auto v106 = pybind11_weaver::CreateEntity<Entity_clang_parseTranslationUnit>(
      EntityScope(m), registry);
  auto v107 = pybind11_weaver::CreateEntity<Entity_clang_parseTranslationUnit2>(
      EntityScope(m), registry);
  auto v108 =
      pybind11_weaver::CreateEntity<Entity_clang_parseTranslationUnit2FullArgv>(
          EntityScope(m), registry);
  auto v109 = pybind11_weaver::CreateEntity<Entity_CXSaveTranslationUnit_Flags>(
      EntityScope(m), registry);
  auto v110 = pybind11_weaver::CreateEntity<Entity_clang_defaultSaveOptions>(
      EntityScope(m), registry);
  auto v111 = pybind11_weaver::CreateEntity<Entity_CXSaveError>(EntityScope(m),
                                                                registry);
  auto v112 = pybind11_weaver::CreateEntity<Entity_clang_saveTranslationUnit>(
      EntityScope(m), registry);
  auto v113 =
      pybind11_weaver::CreateEntity<Entity_clang_suspendTranslationUnit>(
          EntityScope(m), registry);
  auto v114 =
      pybind11_weaver::CreateEntity<Entity_clang_disposeTranslationUnit>(
          EntityScope(m), registry);
  auto v115 = pybind11_weaver::CreateEntity<Entity_CXReparse_Flags>(
      EntityScope(m), registry);
  auto v116 = pybind11_weaver::CreateEntity<Entity_clang_defaultReparseOptions>(
      EntityScope(m), registry);
  auto v117 =
      pybind11_weaver::CreateEntity<Entity_clang_reparseTranslationUnit>(
          EntityScope(m), registry);
  auto v118 = pybind11_weaver::CreateEntity<Entity_CXTUResourceUsageKind>(
      EntityScope(m), registry);
  auto v119 =
      pybind11_weaver::CreateEntity<Entity_clang_getTUResourceUsageName>(
          EntityScope(m), registry);
  auto v120 = pybind11_weaver::CreateEntity<Entity_CXTUResourceUsageEntry>(
      EntityScope(m), registry);
  auto v121 = pybind11_weaver::CreateEntity<Entity_CXTUResourceUsage>(
      EntityScope(m), registry);
  auto v122 = pybind11_weaver::CreateEntity<Entity_clang_getCXTUResourceUsage>(
      EntityScope(m), registry);
  auto v123 =
      pybind11_weaver::CreateEntity<Entity_clang_disposeCXTUResourceUsage>(
          EntityScope(m), registry);
  auto v124 =
      pybind11_weaver::CreateEntity<Entity_clang_getTranslationUnitTargetInfo>(
          EntityScope(m), registry);
  auto v125 = pybind11_weaver::CreateEntity<Entity_clang_TargetInfo_dispose>(
      EntityScope(m), registry);
  auto v126 = pybind11_weaver::CreateEntity<Entity_clang_TargetInfo_getTriple>(
      EntityScope(m), registry);
  auto v127 =
      pybind11_weaver::CreateEntity<Entity_clang_TargetInfo_getPointerWidth>(
          EntityScope(m), registry);
  auto v128 = pybind11_weaver::CreateEntity<Entity_CXCursorKind>(EntityScope(m),
                                                                 registry);
  auto v129 =
      pybind11_weaver::CreateEntity<Entity_CXCursor>(EntityScope(m), registry);
  auto v130 = pybind11_weaver::CreateEntity<Entity_clang_getNullCursor>(
      EntityScope(m), registry);
  auto v131 =
      pybind11_weaver::CreateEntity<Entity_clang_getTranslationUnitCursor>(
          EntityScope(m), registry);
  auto v132 = pybind11_weaver::CreateEntity<Entity_clang_equalCursors>(
      EntityScope(m), registry);
  auto v133 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_isNull>(
      EntityScope(m), registry);
  auto v134 = pybind11_weaver::CreateEntity<Entity_clang_hashCursor>(
      EntityScope(m), registry);
  auto v135 = pybind11_weaver::CreateEntity<Entity_clang_getCursorKind>(
      EntityScope(m), registry);
  auto v136 = pybind11_weaver::CreateEntity<Entity_clang_isDeclaration>(
      EntityScope(m), registry);
  auto v137 = pybind11_weaver::CreateEntity<Entity_clang_isInvalidDeclaration>(
      EntityScope(m), registry);
  auto v138 = pybind11_weaver::CreateEntity<Entity_clang_isReference>(
      EntityScope(m), registry);
  auto v139 = pybind11_weaver::CreateEntity<Entity_clang_isExpression>(
      EntityScope(m), registry);
  auto v140 = pybind11_weaver::CreateEntity<Entity_clang_isStatement>(
      EntityScope(m), registry);
  auto v141 = pybind11_weaver::CreateEntity<Entity_clang_isAttribute>(
      EntityScope(m), registry);
  auto v142 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_hasAttrs>(
      EntityScope(m), registry);
  auto v143 = pybind11_weaver::CreateEntity<Entity_clang_isInvalid>(
      EntityScope(m), registry);
  auto v144 = pybind11_weaver::CreateEntity<Entity_clang_isTranslationUnit>(
      EntityScope(m), registry);
  auto v145 = pybind11_weaver::CreateEntity<Entity_clang_isPreprocessing>(
      EntityScope(m), registry);
  auto v146 = pybind11_weaver::CreateEntity<Entity_clang_isUnexposed>(
      EntityScope(m), registry);
  auto v147 = pybind11_weaver::CreateEntity<Entity_CXLinkageKind>(
      EntityScope(m), registry);
  auto v148 = pybind11_weaver::CreateEntity<Entity_clang_getCursorLinkage>(
      EntityScope(m), registry);
  auto v149 = pybind11_weaver::CreateEntity<Entity_CXVisibilityKind>(
      EntityScope(m), registry);
  auto v150 = pybind11_weaver::CreateEntity<Entity_clang_getCursorVisibility>(
      EntityScope(m), registry);
  auto v151 = pybind11_weaver::CreateEntity<Entity_clang_getCursorAvailability>(
      EntityScope(m), registry);
  auto v152 = pybind11_weaver::CreateEntity<Entity_CXPlatformAvailability>(
      EntityScope(m), registry);
  auto v153 =
      pybind11_weaver::CreateEntity<Entity_clang_getCursorPlatformAvailability>(
          EntityScope(m), registry);
  auto v154 =
      pybind11_weaver::CreateEntity<Entity_clang_disposeCXPlatformAvailability>(
          EntityScope(m), registry);
  auto v155 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getVarDeclInitializer>(
          EntityScope(m), registry);
  auto v156 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_hasVarDeclGlobalStorage>(EntityScope(m), registry);
  auto v157 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_hasVarDeclExternalStorage>(EntityScope(m), registry);
  auto v158 = pybind11_weaver::CreateEntity<Entity_CXLanguageKind>(
      EntityScope(m), registry);
  auto v159 = pybind11_weaver::CreateEntity<Entity_clang_getCursorLanguage>(
      EntityScope(m), registry);
  auto v160 =
      pybind11_weaver::CreateEntity<Entity_CXTLSKind>(EntityScope(m), registry);
  auto v161 = pybind11_weaver::CreateEntity<Entity_clang_getCursorTLSKind>(
      EntityScope(m), registry);
  auto v162 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getTranslationUnit>(
          EntityScope(m), registry);
  auto v163 = pybind11_weaver::CreateEntity<Entity_clang_createCXCursorSet>(
      EntityScope(m), registry);
  auto v164 = pybind11_weaver::CreateEntity<Entity_clang_disposeCXCursorSet>(
      EntityScope(m), registry);
  auto v165 = pybind11_weaver::CreateEntity<Entity_clang_CXCursorSet_contains>(
      EntityScope(m), registry);
  auto v166 = pybind11_weaver::CreateEntity<Entity_clang_CXCursorSet_insert>(
      EntityScope(m), registry);
  auto v167 =
      pybind11_weaver::CreateEntity<Entity_clang_getCursorSemanticParent>(
          EntityScope(m), registry);
  auto v168 =
      pybind11_weaver::CreateEntity<Entity_clang_getCursorLexicalParent>(
          EntityScope(m), registry);
  auto v169 = pybind11_weaver::CreateEntity<Entity_clang_getOverriddenCursors>(
      EntityScope(m), registry);
  auto v170 =
      pybind11_weaver::CreateEntity<Entity_clang_disposeOverriddenCursors>(
          EntityScope(m), registry);
  auto v171 = pybind11_weaver::CreateEntity<Entity_clang_getIncludedFile>(
      EntityScope(m), registry);
  auto v172 = pybind11_weaver::CreateEntity<Entity_clang_getCursor>(
      EntityScope(m), registry);
  auto v173 = pybind11_weaver::CreateEntity<Entity_clang_getCursorLocation>(
      EntityScope(m), registry);
  auto v174 = pybind11_weaver::CreateEntity<Entity_clang_getCursorExtent>(
      EntityScope(m), registry);
  auto v175 = pybind11_weaver::CreateEntity<Entity_CXTypeKind>(EntityScope(m),
                                                               registry);
  auto v176 = pybind11_weaver::CreateEntity<Entity_CXCallingConv>(
      EntityScope(m), registry);
  auto v177 =
      pybind11_weaver::CreateEntity<Entity_CXType>(EntityScope(m), registry);
  auto v178 = pybind11_weaver::CreateEntity<Entity_clang_getCursorType>(
      EntityScope(m), registry);
  auto v179 = pybind11_weaver::CreateEntity<Entity_clang_getTypeSpelling>(
      EntityScope(m), registry);
  auto v180 =
      pybind11_weaver::CreateEntity<Entity_clang_getTypedefDeclUnderlyingType>(
          EntityScope(m), registry);
  auto v181 =
      pybind11_weaver::CreateEntity<Entity_clang_getEnumDeclIntegerType>(
          EntityScope(m), registry);
  auto v182 =
      pybind11_weaver::CreateEntity<Entity_clang_getEnumConstantDeclValue>(
          EntityScope(m), registry);
  auto v183 = pybind11_weaver::CreateEntity<
      Entity_clang_getEnumConstantDeclUnsignedValue>(EntityScope(m), registry);
  auto v184 = pybind11_weaver::CreateEntity<Entity_clang_getFieldDeclBitWidth>(
      EntityScope(m), registry);
  auto v185 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getNumArguments>(
          EntityScope(m), registry);
  auto v186 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_getArgument>(
      EntityScope(m), registry);
  auto v187 = pybind11_weaver::CreateEntity<Entity_CXTemplateArgumentKind>(
      EntityScope(m), registry);
  auto v188 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_getNumTemplateArguments>(EntityScope(m), registry);
  auto v189 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_getTemplateArgumentKind>(EntityScope(m), registry);
  auto v190 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_getTemplateArgumentType>(EntityScope(m), registry);
  auto v191 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_getTemplateArgumentValue>(EntityScope(m), registry);
  auto v192 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_getTemplateArgumentUnsignedValue>(EntityScope(m),
                                                            registry);
  auto v193 = pybind11_weaver::CreateEntity<Entity_clang_equalTypes>(
      EntityScope(m), registry);
  auto v194 = pybind11_weaver::CreateEntity<Entity_clang_getCanonicalType>(
      EntityScope(m), registry);
  auto v195 = pybind11_weaver::CreateEntity<Entity_clang_isConstQualifiedType>(
      EntityScope(m), registry);
  auto v196 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_isMacroFunctionLike>(
          EntityScope(m), registry);
  auto v197 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_isMacroBuiltin>(
      EntityScope(m), registry);
  auto v198 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_isFunctionInlined>(
          EntityScope(m), registry);
  auto v199 =
      pybind11_weaver::CreateEntity<Entity_clang_isVolatileQualifiedType>(
          EntityScope(m), registry);
  auto v200 =
      pybind11_weaver::CreateEntity<Entity_clang_isRestrictQualifiedType>(
          EntityScope(m), registry);
  auto v201 = pybind11_weaver::CreateEntity<Entity_clang_getAddressSpace>(
      EntityScope(m), registry);
  auto v202 = pybind11_weaver::CreateEntity<Entity_clang_getTypedefName>(
      EntityScope(m), registry);
  auto v203 = pybind11_weaver::CreateEntity<Entity_clang_getPointeeType>(
      EntityScope(m), registry);
  auto v204 = pybind11_weaver::CreateEntity<Entity_clang_getUnqualifiedType>(
      EntityScope(m), registry);
  auto v205 = pybind11_weaver::CreateEntity<Entity_clang_getNonReferenceType>(
      EntityScope(m), registry);
  auto v206 = pybind11_weaver::CreateEntity<Entity_clang_getTypeDeclaration>(
      EntityScope(m), registry);
  auto v207 =
      pybind11_weaver::CreateEntity<Entity_clang_getDeclObjCTypeEncoding>(
          EntityScope(m), registry);
  auto v208 = pybind11_weaver::CreateEntity<Entity_clang_Type_getObjCEncoding>(
      EntityScope(m), registry);
  auto v209 = pybind11_weaver::CreateEntity<Entity_clang_getTypeKindSpelling>(
      EntityScope(m), registry);
  auto v210 =
      pybind11_weaver::CreateEntity<Entity_clang_getFunctionTypeCallingConv>(
          EntityScope(m), registry);
  auto v211 = pybind11_weaver::CreateEntity<Entity_clang_getResultType>(
      EntityScope(m), registry);
  auto v212 =
      pybind11_weaver::CreateEntity<Entity_clang_getExceptionSpecificationType>(
          EntityScope(m), registry);
  auto v213 = pybind11_weaver::CreateEntity<Entity_clang_getNumArgTypes>(
      EntityScope(m), registry);
  auto v214 = pybind11_weaver::CreateEntity<Entity_clang_getArgType>(
      EntityScope(m), registry);
  auto v215 =
      pybind11_weaver::CreateEntity<Entity_clang_Type_getObjCObjectBaseType>(
          EntityScope(m), registry);
  auto v216 =
      pybind11_weaver::CreateEntity<Entity_clang_Type_getNumObjCProtocolRefs>(
          EntityScope(m), registry);
  auto v217 =
      pybind11_weaver::CreateEntity<Entity_clang_Type_getObjCProtocolDecl>(
          EntityScope(m), registry);
  auto v218 =
      pybind11_weaver::CreateEntity<Entity_clang_Type_getNumObjCTypeArgs>(
          EntityScope(m), registry);
  auto v219 = pybind11_weaver::CreateEntity<Entity_clang_Type_getObjCTypeArg>(
      EntityScope(m), registry);
  auto v220 =
      pybind11_weaver::CreateEntity<Entity_clang_isFunctionTypeVariadic>(
          EntityScope(m), registry);
  auto v221 = pybind11_weaver::CreateEntity<Entity_clang_getCursorResultType>(
      EntityScope(m), registry);
  auto v222 = pybind11_weaver::CreateEntity<
      Entity_clang_getCursorExceptionSpecificationType>(EntityScope(m),
                                                        registry);
  auto v223 = pybind11_weaver::CreateEntity<Entity_clang_isPODType>(
      EntityScope(m), registry);
  auto v224 = pybind11_weaver::CreateEntity<Entity_clang_getElementType>(
      EntityScope(m), registry);
  auto v225 = pybind11_weaver::CreateEntity<Entity_clang_getNumElements>(
      EntityScope(m), registry);
  auto v226 = pybind11_weaver::CreateEntity<Entity_clang_getArrayElementType>(
      EntityScope(m), registry);
  auto v227 = pybind11_weaver::CreateEntity<Entity_clang_getArraySize>(
      EntityScope(m), registry);
  auto v228 = pybind11_weaver::CreateEntity<Entity_clang_Type_getNamedType>(
      EntityScope(m), registry);
  auto v229 =
      pybind11_weaver::CreateEntity<Entity_clang_Type_isTransparentTagTypedef>(
          EntityScope(m), registry);
  auto v230 = pybind11_weaver::CreateEntity<Entity_CXTypeNullabilityKind>(
      EntityScope(m), registry);
  auto v231 = pybind11_weaver::CreateEntity<Entity_clang_Type_getNullability>(
      EntityScope(m), registry);
  auto v232 = pybind11_weaver::CreateEntity<Entity_CXTypeLayoutError>(
      EntityScope(m), registry);
  auto v233 = pybind11_weaver::CreateEntity<Entity_clang_Type_getAlignOf>(
      EntityScope(m), registry);
  auto v234 = pybind11_weaver::CreateEntity<Entity_clang_Type_getClassType>(
      EntityScope(m), registry);
  auto v235 = pybind11_weaver::CreateEntity<Entity_clang_Type_getSizeOf>(
      EntityScope(m), registry);
  auto v236 = pybind11_weaver::CreateEntity<Entity_clang_Type_getOffsetOf>(
      EntityScope(m), registry);
  auto v237 = pybind11_weaver::CreateEntity<Entity_clang_Type_getModifiedType>(
      EntityScope(m), registry);
  auto v238 = pybind11_weaver::CreateEntity<Entity_clang_Type_getValueType>(
      EntityScope(m), registry);
  auto v239 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getOffsetOfField>(
          EntityScope(m), registry);
  auto v240 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_isAnonymous>(
      EntityScope(m), registry);
  auto v241 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_isAnonymousRecordDecl>(
          EntityScope(m), registry);
  auto v242 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_isInlineNamespace>(
          EntityScope(m), registry);
  auto v243 = pybind11_weaver::CreateEntity<Entity_CXRefQualifierKind>(
      EntityScope(m), registry);
  auto v244 =
      pybind11_weaver::CreateEntity<Entity_clang_Type_getNumTemplateArguments>(
          EntityScope(m), registry);
  auto v245 = pybind11_weaver::CreateEntity<
      Entity_clang_Type_getTemplateArgumentAsType>(EntityScope(m), registry);
  auto v246 =
      pybind11_weaver::CreateEntity<Entity_clang_Type_getCXXRefQualifier>(
          EntityScope(m), registry);
  auto v247 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_isBitField>(
      EntityScope(m), registry);
  auto v248 = pybind11_weaver::CreateEntity<Entity_clang_isVirtualBase>(
      EntityScope(m), registry);
  auto v249 = pybind11_weaver::CreateEntity<Entity_CX_CXXAccessSpecifier>(
      EntityScope(m), registry);
  auto v250 = pybind11_weaver::CreateEntity<Entity_clang_getCXXAccessSpecifier>(
      EntityScope(m), registry);
  auto v251 = pybind11_weaver::CreateEntity<Entity_CX_StorageClass>(
      EntityScope(m), registry);
  auto v252 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getStorageClass>(
          EntityScope(m), registry);
  auto v253 = pybind11_weaver::CreateEntity<Entity_clang_getNumOverloadedDecls>(
      EntityScope(m), registry);
  auto v254 = pybind11_weaver::CreateEntity<Entity_clang_getOverloadedDecl>(
      EntityScope(m), registry);
  auto v255 =
      pybind11_weaver::CreateEntity<Entity_clang_getIBOutletCollectionType>(
          EntityScope(m), registry);
  auto v256 = pybind11_weaver::CreateEntity<Entity_CXChildVisitResult>(
      EntityScope(m), registry);
  auto v257 = pybind11_weaver::CreateEntity<Entity_clang_visitChildren>(
      EntityScope(m), registry);
  auto v258 = pybind11_weaver::CreateEntity<Entity_clang_getCursorUSR>(
      EntityScope(m), registry);
  auto v259 =
      pybind11_weaver::CreateEntity<Entity_clang_constructUSR_ObjCClass>(
          EntityScope(m), registry);
  auto v260 =
      pybind11_weaver::CreateEntity<Entity_clang_constructUSR_ObjCCategory>(
          EntityScope(m), registry);
  auto v261 =
      pybind11_weaver::CreateEntity<Entity_clang_constructUSR_ObjCProtocol>(
          EntityScope(m), registry);
  auto v262 = pybind11_weaver::CreateEntity<Entity_clang_constructUSR_ObjCIvar>(
      EntityScope(m), registry);
  auto v263 =
      pybind11_weaver::CreateEntity<Entity_clang_constructUSR_ObjCMethod>(
          EntityScope(m), registry);
  auto v264 =
      pybind11_weaver::CreateEntity<Entity_clang_constructUSR_ObjCProperty>(
          EntityScope(m), registry);
  auto v265 = pybind11_weaver::CreateEntity<Entity_clang_getCursorSpelling>(
      EntityScope(m), registry);
  auto v266 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getSpellingNameRange>(
          EntityScope(m), registry);
  auto v267 = pybind11_weaver::CreateEntity<Entity_CXPrintingPolicyProperty>(
      EntityScope(m), registry);
  auto v268 =
      pybind11_weaver::CreateEntity<Entity_clang_PrintingPolicy_getProperty>(
          EntityScope(m), registry);
  auto v269 =
      pybind11_weaver::CreateEntity<Entity_clang_PrintingPolicy_setProperty>(
          EntityScope(m), registry);
  auto v270 =
      pybind11_weaver::CreateEntity<Entity_clang_getCursorPrintingPolicy>(
          EntityScope(m), registry);
  auto v271 =
      pybind11_weaver::CreateEntity<Entity_clang_PrintingPolicy_dispose>(
          EntityScope(m), registry);
  auto v272 =
      pybind11_weaver::CreateEntity<Entity_clang_getCursorPrettyPrinted>(
          EntityScope(m), registry);
  auto v273 = pybind11_weaver::CreateEntity<Entity_clang_getCursorDisplayName>(
      EntityScope(m), registry);
  auto v274 = pybind11_weaver::CreateEntity<Entity_clang_getCursorReferenced>(
      EntityScope(m), registry);
  auto v275 = pybind11_weaver::CreateEntity<Entity_clang_getCursorDefinition>(
      EntityScope(m), registry);
  auto v276 = pybind11_weaver::CreateEntity<Entity_clang_isCursorDefinition>(
      EntityScope(m), registry);
  auto v277 = pybind11_weaver::CreateEntity<Entity_clang_getCanonicalCursor>(
      EntityScope(m), registry);
  auto v278 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getObjCSelectorIndex>(
          EntityScope(m), registry);
  auto v279 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_isDynamicCall>(
      EntityScope(m), registry);
  auto v280 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getReceiverType>(
          EntityScope(m), registry);
  auto v281 = pybind11_weaver::CreateEntity<Entity_CXObjCPropertyAttrKind>(
      EntityScope(m), registry);
  auto v282 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_getObjCPropertyAttributes>(EntityScope(m), registry);
  auto v283 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_getObjCPropertyGetterName>(EntityScope(m), registry);
  auto v284 = pybind11_weaver::CreateEntity<
      Entity_clang_Cursor_getObjCPropertySetterName>(EntityScope(m), registry);
  auto v285 = pybind11_weaver::CreateEntity<Entity_CXObjCDeclQualifierKind>(
      EntityScope(m), registry);
  auto v286 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getObjCDeclQualifiers>(
          EntityScope(m), registry);
  auto v287 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_isObjCOptional>(
      EntityScope(m), registry);
  auto v288 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_isVariadic>(
      EntityScope(m), registry);
  auto v289 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_isExternalSymbol>(
          EntityScope(m), registry);
  auto v290 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getCommentRange>(
          EntityScope(m), registry);
  auto v291 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getRawCommentText>(
          EntityScope(m), registry);
  auto v292 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getBriefCommentText>(
          EntityScope(m), registry);
  auto v293 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_getMangling>(
      EntityScope(m), registry);
  auto v294 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getCXXManglings>(
          EntityScope(m), registry);
  auto v295 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getObjCManglings>(
          EntityScope(m), registry);
  auto v296 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_getModule>(
      EntityScope(m), registry);
  auto v297 = pybind11_weaver::CreateEntity<Entity_clang_getModuleForFile>(
      EntityScope(m), registry);
  auto v298 = pybind11_weaver::CreateEntity<Entity_clang_Module_getASTFile>(
      EntityScope(m), registry);
  auto v299 = pybind11_weaver::CreateEntity<Entity_clang_Module_getParent>(
      EntityScope(m), registry);
  auto v300 = pybind11_weaver::CreateEntity<Entity_clang_Module_getName>(
      EntityScope(m), registry);
  auto v301 = pybind11_weaver::CreateEntity<Entity_clang_Module_getFullName>(
      EntityScope(m), registry);
  auto v302 = pybind11_weaver::CreateEntity<Entity_clang_Module_isSystem>(
      EntityScope(m), registry);
  auto v303 =
      pybind11_weaver::CreateEntity<Entity_clang_Module_getNumTopLevelHeaders>(
          EntityScope(m), registry);
  auto v304 =
      pybind11_weaver::CreateEntity<Entity_clang_Module_getTopLevelHeader>(
          EntityScope(m), registry);
  auto v305 = pybind11_weaver::CreateEntity<
      Entity_clang_CXXConstructor_isConvertingConstructor>(EntityScope(m),
                                                           registry);
  auto v306 = pybind11_weaver::CreateEntity<
      Entity_clang_CXXConstructor_isCopyConstructor>(EntityScope(m), registry);
  auto v307 = pybind11_weaver::CreateEntity<
      Entity_clang_CXXConstructor_isDefaultConstructor>(EntityScope(m),
                                                        registry);
  auto v308 = pybind11_weaver::CreateEntity<
      Entity_clang_CXXConstructor_isMoveConstructor>(EntityScope(m), registry);
  auto v309 = pybind11_weaver::CreateEntity<Entity_clang_CXXField_isMutable>(
      EntityScope(m), registry);
  auto v310 = pybind11_weaver::CreateEntity<Entity_clang_CXXMethod_isDefaulted>(
      EntityScope(m), registry);
  auto v311 = pybind11_weaver::CreateEntity<Entity_clang_CXXMethod_isDeleted>(
      EntityScope(m), registry);
  auto v312 =
      pybind11_weaver::CreateEntity<Entity_clang_CXXMethod_isPureVirtual>(
          EntityScope(m), registry);
  auto v313 = pybind11_weaver::CreateEntity<Entity_clang_CXXMethod_isStatic>(
      EntityScope(m), registry);
  auto v314 = pybind11_weaver::CreateEntity<Entity_clang_CXXMethod_isVirtual>(
      EntityScope(m), registry);
  auto v315 = pybind11_weaver::CreateEntity<
      Entity_clang_CXXMethod_isCopyAssignmentOperator>(EntityScope(m),
                                                       registry);
  auto v316 = pybind11_weaver::CreateEntity<
      Entity_clang_CXXMethod_isMoveAssignmentOperator>(EntityScope(m),
                                                       registry);
  auto v317 = pybind11_weaver::CreateEntity<Entity_clang_CXXRecord_isAbstract>(
      EntityScope(m), registry);
  auto v318 = pybind11_weaver::CreateEntity<Entity_clang_EnumDecl_isScoped>(
      EntityScope(m), registry);
  auto v319 = pybind11_weaver::CreateEntity<Entity_clang_CXXMethod_isConst>(
      EntityScope(m), registry);
  auto v320 = pybind11_weaver::CreateEntity<Entity_clang_getTemplateCursorKind>(
      EntityScope(m), registry);
  auto v321 =
      pybind11_weaver::CreateEntity<Entity_clang_getSpecializedCursorTemplate>(
          EntityScope(m), registry);
  auto v322 =
      pybind11_weaver::CreateEntity<Entity_clang_getCursorReferenceNameRange>(
          EntityScope(m), registry);
  auto v323 = pybind11_weaver::CreateEntity<Entity_CXNameRefFlags>(
      EntityScope(m), registry);
  auto v324 = pybind11_weaver::CreateEntity<Entity_CXTokenKind>(EntityScope(m),
                                                                registry);
  auto v325 =
      pybind11_weaver::CreateEntity<Entity_CXToken>(EntityScope(m), registry);
  auto v326 = pybind11_weaver::CreateEntity<Entity_clang_getToken>(
      EntityScope(m), registry);
  auto v327 = pybind11_weaver::CreateEntity<Entity_clang_getTokenKind>(
      EntityScope(m), registry);
  auto v328 = pybind11_weaver::CreateEntity<Entity_clang_getTokenSpelling>(
      EntityScope(m), registry);
  auto v329 = pybind11_weaver::CreateEntity<Entity_clang_getTokenLocation>(
      EntityScope(m), registry);
  auto v330 = pybind11_weaver::CreateEntity<Entity_clang_getTokenExtent>(
      EntityScope(m), registry);
  auto v331 = pybind11_weaver::CreateEntity<Entity_clang_tokenize>(
      EntityScope(m), registry);
  auto v332 = pybind11_weaver::CreateEntity<Entity_clang_annotateTokens>(
      EntityScope(m), registry);
  auto v333 = pybind11_weaver::CreateEntity<Entity_clang_disposeTokens>(
      EntityScope(m), registry);
  auto v334 = pybind11_weaver::CreateEntity<Entity_clang_getCursorKindSpelling>(
      EntityScope(m), registry);
  auto v335 = pybind11_weaver::CreateEntity<
      Entity_clang_getDefinitionSpellingAndExtent>(EntityScope(m), registry);
  auto v336 = pybind11_weaver::CreateEntity<Entity_clang_enableStackTraces>(
      EntityScope(m), registry);
  auto v337 = pybind11_weaver::CreateEntity<Entity_clang_executeOnThread>(
      EntityScope(m), registry);
  auto v338 = pybind11_weaver::CreateEntity<Entity_CXCompletionResult>(
      EntityScope(m), registry);
  auto v339 = pybind11_weaver::CreateEntity<Entity_CXCompletionChunkKind>(
      EntityScope(m), registry);
  auto v340 =
      pybind11_weaver::CreateEntity<Entity_clang_getCompletionChunkKind>(
          EntityScope(m), registry);
  auto v341 =
      pybind11_weaver::CreateEntity<Entity_clang_getCompletionChunkText>(
          EntityScope(m), registry);
  auto v342 = pybind11_weaver::CreateEntity<
      Entity_clang_getCompletionChunkCompletionString>(EntityScope(m),
                                                       registry);
  auto v343 =
      pybind11_weaver::CreateEntity<Entity_clang_getNumCompletionChunks>(
          EntityScope(m), registry);
  auto v344 = pybind11_weaver::CreateEntity<Entity_clang_getCompletionPriority>(
      EntityScope(m), registry);
  auto v345 =
      pybind11_weaver::CreateEntity<Entity_clang_getCompletionAvailability>(
          EntityScope(m), registry);
  auto v346 =
      pybind11_weaver::CreateEntity<Entity_clang_getCompletionNumAnnotations>(
          EntityScope(m), registry);
  auto v347 =
      pybind11_weaver::CreateEntity<Entity_clang_getCompletionAnnotation>(
          EntityScope(m), registry);
  auto v348 = pybind11_weaver::CreateEntity<Entity_clang_getCompletionParent>(
      EntityScope(m), registry);
  auto v349 =
      pybind11_weaver::CreateEntity<Entity_clang_getCompletionBriefComment>(
          EntityScope(m), registry);
  auto v350 =
      pybind11_weaver::CreateEntity<Entity_clang_getCursorCompletionString>(
          EntityScope(m), registry);
  auto v351 = pybind11_weaver::CreateEntity<Entity_CXCodeCompleteResults>(
      EntityScope(m), registry);
  auto v352 =
      pybind11_weaver::CreateEntity<Entity_clang_getCompletionNumFixIts>(
          EntityScope(m), registry);
  auto v353 = pybind11_weaver::CreateEntity<Entity_clang_getCompletionFixIt>(
      EntityScope(m), registry);
  auto v354 = pybind11_weaver::CreateEntity<Entity_CXCodeComplete_Flags>(
      EntityScope(m), registry);
  auto v355 = pybind11_weaver::CreateEntity<Entity_CXCompletionContext>(
      EntityScope(m), registry);
  auto v356 =
      pybind11_weaver::CreateEntity<Entity_clang_defaultCodeCompleteOptions>(
          EntityScope(m), registry);
  auto v357 = pybind11_weaver::CreateEntity<Entity_clang_codeCompleteAt>(
      EntityScope(m), registry);
  auto v358 =
      pybind11_weaver::CreateEntity<Entity_clang_sortCodeCompletionResults>(
          EntityScope(m), registry);
  auto v359 =
      pybind11_weaver::CreateEntity<Entity_clang_disposeCodeCompleteResults>(
          EntityScope(m), registry);
  auto v360 =
      pybind11_weaver::CreateEntity<Entity_clang_codeCompleteGetNumDiagnostics>(
          EntityScope(m), registry);
  auto v361 =
      pybind11_weaver::CreateEntity<Entity_clang_codeCompleteGetDiagnostic>(
          EntityScope(m), registry);
  auto v362 =
      pybind11_weaver::CreateEntity<Entity_clang_codeCompleteGetContexts>(
          EntityScope(m), registry);
  auto v363 =
      pybind11_weaver::CreateEntity<Entity_clang_codeCompleteGetContainerKind>(
          EntityScope(m), registry);
  auto v364 =
      pybind11_weaver::CreateEntity<Entity_clang_codeCompleteGetContainerUSR>(
          EntityScope(m), registry);
  auto v365 =
      pybind11_weaver::CreateEntity<Entity_clang_codeCompleteGetObjCSelector>(
          EntityScope(m), registry);
  auto v366 = pybind11_weaver::CreateEntity<Entity_clang_getClangVersion>(
      EntityScope(m), registry);
  auto v367 = pybind11_weaver::CreateEntity<Entity_clang_toggleCrashRecovery>(
      EntityScope(m), registry);
  auto v368 = pybind11_weaver::CreateEntity<Entity_clang_getInclusions>(
      EntityScope(m), registry);
  auto v369 = pybind11_weaver::CreateEntity<Entity_CXEvalResultKind>(
      EntityScope(m), registry);
  auto v370 = pybind11_weaver::CreateEntity<Entity_clang_Cursor_Evaluate>(
      EntityScope(m), registry);
  auto v371 = pybind11_weaver::CreateEntity<Entity_clang_EvalResult_getKind>(
      EntityScope(m), registry);
  auto v372 = pybind11_weaver::CreateEntity<Entity_clang_EvalResult_getAsInt>(
      EntityScope(m), registry);
  auto v373 =
      pybind11_weaver::CreateEntity<Entity_clang_EvalResult_getAsLongLong>(
          EntityScope(m), registry);
  auto v374 =
      pybind11_weaver::CreateEntity<Entity_clang_EvalResult_isUnsignedInt>(
          EntityScope(m), registry);
  auto v375 =
      pybind11_weaver::CreateEntity<Entity_clang_EvalResult_getAsUnsigned>(
          EntityScope(m), registry);
  auto v376 =
      pybind11_weaver::CreateEntity<Entity_clang_EvalResult_getAsDouble>(
          EntityScope(m), registry);
  auto v377 = pybind11_weaver::CreateEntity<Entity_clang_EvalResult_getAsStr>(
      EntityScope(m), registry);
  auto v378 = pybind11_weaver::CreateEntity<Entity_clang_EvalResult_dispose>(
      EntityScope(m), registry);
  auto v379 = pybind11_weaver::CreateEntity<Entity_clang_getRemappings>(
      EntityScope(m), registry);
  auto v380 =
      pybind11_weaver::CreateEntity<Entity_clang_getRemappingsFromFileList>(
          EntityScope(m), registry);
  auto v381 = pybind11_weaver::CreateEntity<Entity_clang_remap_getNumFiles>(
      EntityScope(m), registry);
  auto v382 = pybind11_weaver::CreateEntity<Entity_clang_remap_getFilenames>(
      EntityScope(m), registry);
  auto v383 = pybind11_weaver::CreateEntity<Entity_clang_remap_dispose>(
      EntityScope(m), registry);
  auto v384 = pybind11_weaver::CreateEntity<Entity_CXVisitorResult>(
      EntityScope(m), registry);
  auto v385 = pybind11_weaver::CreateEntity<Entity_CXCursorAndRangeVisitor>(
      EntityScope(m), registry);
  auto v386 =
      pybind11_weaver::CreateEntity<Entity_CXResult>(EntityScope(m), registry);
  auto v387 = pybind11_weaver::CreateEntity<Entity_clang_findReferencesInFile>(
      EntityScope(m), registry);
  auto v388 = pybind11_weaver::CreateEntity<Entity_clang_findIncludesInFile>(
      EntityScope(m), registry);
  auto v389 =
      pybind11_weaver::CreateEntity<Entity_CXIdxLoc>(EntityScope(m), registry);
  auto v390 = pybind11_weaver::CreateEntity<Entity_CXIdxIncludedFileInfo>(
      EntityScope(m), registry);
  auto v391 = pybind11_weaver::CreateEntity<Entity_CXIdxImportedASTFileInfo>(
      EntityScope(m), registry);
  auto v392 = pybind11_weaver::CreateEntity<Entity_CXIdxEntityKind>(
      EntityScope(m), registry);
  auto v393 = pybind11_weaver::CreateEntity<Entity_CXIdxEntityLanguage>(
      EntityScope(m), registry);
  auto v394 = pybind11_weaver::CreateEntity<Entity_CXIdxEntityCXXTemplateKind>(
      EntityScope(m), registry);
  auto v395 = pybind11_weaver::CreateEntity<Entity_CXIdxAttrKind>(
      EntityScope(m), registry);
  auto v396 = pybind11_weaver::CreateEntity<Entity_CXIdxAttrInfo>(
      EntityScope(m), registry);
  auto v397 = pybind11_weaver::CreateEntity<Entity_CXIdxEntityInfo>(
      EntityScope(m), registry);
  auto v398 = pybind11_weaver::CreateEntity<Entity_CXIdxContainerInfo>(
      EntityScope(m), registry);
  auto v399 =
      pybind11_weaver::CreateEntity<Entity_CXIdxIBOutletCollectionAttrInfo>(
          EntityScope(m), registry);
  auto v400 = pybind11_weaver::CreateEntity<Entity_CXIdxDeclInfoFlags>(
      EntityScope(m), registry);
  auto v401 = pybind11_weaver::CreateEntity<Entity_CXIdxDeclInfo>(
      EntityScope(m), registry);
  auto v402 = pybind11_weaver::CreateEntity<Entity_CXIdxObjCContainerKind>(
      EntityScope(m), registry);
  auto v403 = pybind11_weaver::CreateEntity<Entity_CXIdxObjCContainerDeclInfo>(
      EntityScope(m), registry);
  auto v404 = pybind11_weaver::CreateEntity<Entity_CXIdxBaseClassInfo>(
      EntityScope(m), registry);
  auto v405 = pybind11_weaver::CreateEntity<Entity_CXIdxObjCProtocolRefInfo>(
      EntityScope(m), registry);
  auto v406 =
      pybind11_weaver::CreateEntity<Entity_CXIdxObjCProtocolRefListInfo>(
          EntityScope(m), registry);
  auto v407 = pybind11_weaver::CreateEntity<Entity_CXIdxObjCInterfaceDeclInfo>(
      EntityScope(m), registry);
  auto v408 = pybind11_weaver::CreateEntity<Entity_CXIdxObjCCategoryDeclInfo>(
      EntityScope(m), registry);
  auto v409 = pybind11_weaver::CreateEntity<Entity_CXIdxObjCPropertyDeclInfo>(
      EntityScope(m), registry);
  auto v410 = pybind11_weaver::CreateEntity<Entity_CXIdxCXXClassDeclInfo>(
      EntityScope(m), registry);
  auto v411 = pybind11_weaver::CreateEntity<Entity_CXIdxEntityRefKind>(
      EntityScope(m), registry);
  auto v412 = pybind11_weaver::CreateEntity<Entity_CXSymbolRole>(EntityScope(m),
                                                                 registry);
  auto v413 = pybind11_weaver::CreateEntity<Entity_CXIdxEntityRefInfo>(
      EntityScope(m), registry);
  auto v414 = pybind11_weaver::CreateEntity<Entity_IndexerCallbacks>(
      EntityScope(m), registry);
  auto v415 = pybind11_weaver::CreateEntity<
      Entity_clang_index_isEntityObjCContainerKind>(EntityScope(m), registry);
  auto v416 = pybind11_weaver::CreateEntity<
      Entity_clang_index_getObjCContainerDeclInfo>(EntityScope(m), registry);
  auto v417 = pybind11_weaver::CreateEntity<
      Entity_clang_index_getObjCInterfaceDeclInfo>(EntityScope(m), registry);
  auto v418 =
      pybind11_weaver::CreateEntity<Entity_clang_index_getObjCCategoryDeclInfo>(
          EntityScope(m), registry);
  auto v419 = pybind11_weaver::CreateEntity<
      Entity_clang_index_getObjCProtocolRefListInfo>(EntityScope(m), registry);
  auto v420 =
      pybind11_weaver::CreateEntity<Entity_clang_index_getObjCPropertyDeclInfo>(
          EntityScope(m), registry);
  auto v421 = pybind11_weaver::CreateEntity<
      Entity_clang_index_getIBOutletCollectionAttrInfo>(EntityScope(m),
                                                        registry);
  auto v422 =
      pybind11_weaver::CreateEntity<Entity_clang_index_getCXXClassDeclInfo>(
          EntityScope(m), registry);
  auto v423 =
      pybind11_weaver::CreateEntity<Entity_clang_index_getClientContainer>(
          EntityScope(m), registry);
  auto v424 =
      pybind11_weaver::CreateEntity<Entity_clang_index_setClientContainer>(
          EntityScope(m), registry);
  auto v425 = pybind11_weaver::CreateEntity<Entity_clang_index_getClientEntity>(
      EntityScope(m), registry);
  auto v426 = pybind11_weaver::CreateEntity<Entity_clang_index_setClientEntity>(
      EntityScope(m), registry);
  auto v427 = pybind11_weaver::CreateEntity<Entity_clang_IndexAction_create>(
      EntityScope(m), registry);
  auto v428 = pybind11_weaver::CreateEntity<Entity_clang_IndexAction_dispose>(
      EntityScope(m), registry);
  auto v429 = pybind11_weaver::CreateEntity<Entity_CXIndexOptFlags>(
      EntityScope(m), registry);
  auto v430 = pybind11_weaver::CreateEntity<Entity_clang_indexSourceFile>(
      EntityScope(m), registry);
  auto v431 =
      pybind11_weaver::CreateEntity<Entity_clang_indexSourceFileFullArgv>(
          EntityScope(m), registry);
  auto v432 = pybind11_weaver::CreateEntity<Entity_clang_indexTranslationUnit>(
      EntityScope(m), registry);
  auto v433 =
      pybind11_weaver::CreateEntity<Entity_clang_indexLoc_getFileLocation>(
          EntityScope(m), registry);
  auto v434 =
      pybind11_weaver::CreateEntity<Entity_clang_indexLoc_getCXSourceLocation>(
          EntityScope(m), registry);
  auto v435 = pybind11_weaver::CreateEntity<Entity_clang_Type_visitFields>(
      EntityScope(m), registry);
  auto v436 =
      pybind11_weaver::CreateEntity<Entity_CXComment>(EntityScope(m), registry);
  auto v437 =
      pybind11_weaver::CreateEntity<Entity_clang_Cursor_getParsedComment>(
          EntityScope(m), registry);
  auto v438 = pybind11_weaver::CreateEntity<Entity_CXCommentKind>(
      EntityScope(m), registry);
  auto v439 =
      pybind11_weaver::CreateEntity<Entity_CXCommentInlineCommandRenderKind>(
          EntityScope(m), registry);
  auto v440 = pybind11_weaver::CreateEntity<Entity_CXCommentParamPassDirection>(
      EntityScope(m), registry);
  auto v441 = pybind11_weaver::CreateEntity<Entity_clang_Comment_getKind>(
      EntityScope(m), registry);
  auto v442 =
      pybind11_weaver::CreateEntity<Entity_clang_Comment_getNumChildren>(
          EntityScope(m), registry);
  auto v443 = pybind11_weaver::CreateEntity<Entity_clang_Comment_getChild>(
      EntityScope(m), registry);
  auto v444 = pybind11_weaver::CreateEntity<Entity_clang_Comment_isWhitespace>(
      EntityScope(m), registry);
  auto v445 = pybind11_weaver::CreateEntity<
      Entity_clang_InlineContentComment_hasTrailingNewline>(EntityScope(m),
                                                            registry);
  auto v446 = pybind11_weaver::CreateEntity<Entity_clang_TextComment_getText>(
      EntityScope(m), registry);
  auto v447 = pybind11_weaver::CreateEntity<
      Entity_clang_InlineCommandComment_getCommandName>(EntityScope(m),
                                                        registry);
  auto v448 = pybind11_weaver::CreateEntity<
      Entity_clang_InlineCommandComment_getRenderKind>(EntityScope(m),
                                                       registry);
  auto v449 = pybind11_weaver::CreateEntity<
      Entity_clang_InlineCommandComment_getNumArgs>(EntityScope(m), registry);
  auto v450 = pybind11_weaver::CreateEntity<
      Entity_clang_InlineCommandComment_getArgText>(EntityScope(m), registry);
  auto v451 =
      pybind11_weaver::CreateEntity<Entity_clang_HTMLTagComment_getTagName>(
          EntityScope(m), registry);
  auto v452 = pybind11_weaver::CreateEntity<
      Entity_clang_HTMLStartTagComment_isSelfClosing>(EntityScope(m), registry);
  auto v453 =
      pybind11_weaver::CreateEntity<Entity_clang_HTMLStartTag_getNumAttrs>(
          EntityScope(m), registry);
  auto v454 =
      pybind11_weaver::CreateEntity<Entity_clang_HTMLStartTag_getAttrName>(
          EntityScope(m), registry);
  auto v455 =
      pybind11_weaver::CreateEntity<Entity_clang_HTMLStartTag_getAttrValue>(
          EntityScope(m), registry);
  auto v456 = pybind11_weaver::CreateEntity<
      Entity_clang_BlockCommandComment_getCommandName>(EntityScope(m),
                                                       registry);
  auto v457 = pybind11_weaver::CreateEntity<
      Entity_clang_BlockCommandComment_getNumArgs>(EntityScope(m), registry);
  auto v458 = pybind11_weaver::CreateEntity<
      Entity_clang_BlockCommandComment_getArgText>(EntityScope(m), registry);
  auto v459 = pybind11_weaver::CreateEntity<
      Entity_clang_BlockCommandComment_getParagraph>(EntityScope(m), registry);
  auto v460 = pybind11_weaver::CreateEntity<
      Entity_clang_ParamCommandComment_getParamName>(EntityScope(m), registry);
  auto v461 = pybind11_weaver::CreateEntity<
      Entity_clang_ParamCommandComment_isParamIndexValid>(EntityScope(m),
                                                          registry);
  auto v462 = pybind11_weaver::CreateEntity<
      Entity_clang_ParamCommandComment_getParamIndex>(EntityScope(m), registry);
  auto v463 = pybind11_weaver::CreateEntity<
      Entity_clang_ParamCommandComment_isDirectionExplicit>(EntityScope(m),
                                                            registry);
  auto v464 = pybind11_weaver::CreateEntity<
      Entity_clang_ParamCommandComment_getDirection>(EntityScope(m), registry);
  auto v465 = pybind11_weaver::CreateEntity<
      Entity_clang_TParamCommandComment_getParamName>(EntityScope(m), registry);
  auto v466 = pybind11_weaver::CreateEntity<
      Entity_clang_TParamCommandComment_isParamPositionValid>(EntityScope(m),
                                                              registry);
  auto v467 =
      pybind11_weaver::CreateEntity<Entity_clang_TParamCommandComment_getDepth>(
          EntityScope(m), registry);
  auto v468 =
      pybind11_weaver::CreateEntity<Entity_clang_TParamCommandComment_getIndex>(
          EntityScope(m), registry);
  auto v469 = pybind11_weaver::CreateEntity<
      Entity_clang_VerbatimBlockLineComment_getText>(EntityScope(m), registry);
  auto v470 =
      pybind11_weaver::CreateEntity<Entity_clang_VerbatimLineComment_getText>(
          EntityScope(m), registry);
  auto v471 =
      pybind11_weaver::CreateEntity<Entity_clang_HTMLTagComment_getAsString>(
          EntityScope(m), registry);
  auto v472 = pybind11_weaver::CreateEntity<Entity_clang_FullComment_getAsHTML>(
      EntityScope(m), registry);
  auto v473 = pybind11_weaver::CreateEntity<Entity_clang_FullComment_getAsXML>(
      EntityScope(m), registry);
  auto v474 = pybind11_weaver::CreateEntity<Entity_clang_createAPISet>(
      EntityScope(m), registry);
  auto v475 = pybind11_weaver::CreateEntity<Entity_clang_disposeAPISet>(
      EntityScope(m), registry);
  auto v476 = pybind11_weaver::CreateEntity<Entity_clang_getSymbolGraphForUSR>(
      EntityScope(m), registry);
  auto v477 =
      pybind11_weaver::CreateEntity<Entity_clang_getSymbolGraphForCursor>(
          EntityScope(m), registry);
  auto v478 = pybind11_weaver::CreateEntity<
      Entity_clang_install_aborting_llvm_fatal_error_handler>(EntityScope(m),
                                                              registry);
  auto v479 = pybind11_weaver::CreateEntity<
      Entity_clang_uninstall_llvm_fatal_error_handler>(EntityScope(m),
                                                       registry);
  auto v480 = pybind11_weaver::CreateEntity<Entity_clang_CXRewriter_create>(
      EntityScope(m), registry);
  auto v481 =
      pybind11_weaver::CreateEntity<Entity_clang_CXRewriter_insertTextBefore>(
          EntityScope(m), registry);
  auto v482 =
      pybind11_weaver::CreateEntity<Entity_clang_CXRewriter_replaceText>(
          EntityScope(m), registry);
  auto v483 = pybind11_weaver::CreateEntity<Entity_clang_CXRewriter_removeText>(
      EntityScope(m), registry);
  auto v484 = pybind11_weaver::CreateEntity<
      Entity_clang_CXRewriter_overwriteChangedFiles>(EntityScope(m), registry);
  auto v485 = pybind11_weaver::CreateEntity<
      Entity_clang_CXRewriter_writeMainFileToStdOut>(EntityScope(m), registry);
  auto v486 = pybind11_weaver::CreateEntity<Entity_clang_CXRewriter_dispose>(
      EntityScope(m), registry);

  auto update_fn = [=]() {
    v0->Update();
    v1->Update();
    v2->Update();
    v3->Update();
    v4->Update();
    v5->Update();
    v6->Update();
    v7->Update();
    v8->Update();
    v9->Update();
    v10->Update();
    v11->Update();
    v12->Update();
    v13->Update();
    v14->Update();
    v15->Update();
    v16->Update();
    v17->Update();
    v18->Update();
    v19->Update();
    v20->Update();
    v21->Update();
    v22->Update();
    v23->Update();
    v24->Update();
    v25->Update();
    v26->Update();
    v27->Update();
    v28->Update();
    v29->Update();
    v30->Update();
    v31->Update();
    v32->Update();
    v33->Update();
    v34->Update();
    v35->Update();
    v36->Update();
    v37->Update();
    v38->Update();
    v39->Update();
    v40->Update();
    v41->Update();
    v42->Update();
    v43->Update();
    v44->Update();
    v45->Update();
    v46->Update();
    v47->Update();
    v48->Update();
    v49->Update();
    v50->Update();
    v51->Update();
    v52->Update();
    v53->Update();
    v54->Update();
    v55->Update();
    v56->Update();
    v57->Update();
    v58->Update();
    v59->Update();
    v60->Update();
    v61->Update();
    v62->Update();
    v63->Update();
    v64->Update();
    v65->Update();
    v66->Update();
    v67->Update();
    v68->Update();
    v69->Update();
    v70->Update();
    v71->Update();
    v72->Update();
    v73->Update();
    v74->Update();
    v75->Update();
    v76->Update();
    v77->Update();
    v78->Update();
    v79->Update();
    v80->Update();
    v81->Update();
    v82->Update();
    v83->Update();
    v84->Update();
    v85->Update();
    v86->Update();
    v87->Update();
    v88->Update();
    v89->Update();
    v90->Update();
    v91->Update();
    v92->Update();
    v93->Update();
    v94->Update();
    v95->Update();
    v96->Update();
    v97->Update();
    v98->Update();
    v99->Update();
    v100->Update();
    v101->Update();
    v102->Update();
    v103->Update();
    v104->Update();
    v105->Update();
    v106->Update();
    v107->Update();
    v108->Update();
    v109->Update();
    v110->Update();
    v111->Update();
    v112->Update();
    v113->Update();
    v114->Update();
    v115->Update();
    v116->Update();
    v117->Update();
    v118->Update();
    v119->Update();
    v120->Update();
    v121->Update();
    v122->Update();
    v123->Update();
    v124->Update();
    v125->Update();
    v126->Update();
    v127->Update();
    v128->Update();
    v129->Update();
    v130->Update();
    v131->Update();
    v132->Update();
    v133->Update();
    v134->Update();
    v135->Update();
    v136->Update();
    v137->Update();
    v138->Update();
    v139->Update();
    v140->Update();
    v141->Update();
    v142->Update();
    v143->Update();
    v144->Update();
    v145->Update();
    v146->Update();
    v147->Update();
    v148->Update();
    v149->Update();
    v150->Update();
    v151->Update();
    v152->Update();
    v153->Update();
    v154->Update();
    v155->Update();
    v156->Update();
    v157->Update();
    v158->Update();
    v159->Update();
    v160->Update();
    v161->Update();
    v162->Update();
    v163->Update();
    v164->Update();
    v165->Update();
    v166->Update();
    v167->Update();
    v168->Update();
    v169->Update();
    v170->Update();
    v171->Update();
    v172->Update();
    v173->Update();
    v174->Update();
    v175->Update();
    v176->Update();
    v177->Update();
    v178->Update();
    v179->Update();
    v180->Update();
    v181->Update();
    v182->Update();
    v183->Update();
    v184->Update();
    v185->Update();
    v186->Update();
    v187->Update();
    v188->Update();
    v189->Update();
    v190->Update();
    v191->Update();
    v192->Update();
    v193->Update();
    v194->Update();
    v195->Update();
    v196->Update();
    v197->Update();
    v198->Update();
    v199->Update();
    v200->Update();
    v201->Update();
    v202->Update();
    v203->Update();
    v204->Update();
    v205->Update();
    v206->Update();
    v207->Update();
    v208->Update();
    v209->Update();
    v210->Update();
    v211->Update();
    v212->Update();
    v213->Update();
    v214->Update();
    v215->Update();
    v216->Update();
    v217->Update();
    v218->Update();
    v219->Update();
    v220->Update();
    v221->Update();
    v222->Update();
    v223->Update();
    v224->Update();
    v225->Update();
    v226->Update();
    v227->Update();
    v228->Update();
    v229->Update();
    v230->Update();
    v231->Update();
    v232->Update();
    v233->Update();
    v234->Update();
    v235->Update();
    v236->Update();
    v237->Update();
    v238->Update();
    v239->Update();
    v240->Update();
    v241->Update();
    v242->Update();
    v243->Update();
    v244->Update();
    v245->Update();
    v246->Update();
    v247->Update();
    v248->Update();
    v249->Update();
    v250->Update();
    v251->Update();
    v252->Update();
    v253->Update();
    v254->Update();
    v255->Update();
    v256->Update();
    v257->Update();
    v258->Update();
    v259->Update();
    v260->Update();
    v261->Update();
    v262->Update();
    v263->Update();
    v264->Update();
    v265->Update();
    v266->Update();
    v267->Update();
    v268->Update();
    v269->Update();
    v270->Update();
    v271->Update();
    v272->Update();
    v273->Update();
    v274->Update();
    v275->Update();
    v276->Update();
    v277->Update();
    v278->Update();
    v279->Update();
    v280->Update();
    v281->Update();
    v282->Update();
    v283->Update();
    v284->Update();
    v285->Update();
    v286->Update();
    v287->Update();
    v288->Update();
    v289->Update();
    v290->Update();
    v291->Update();
    v292->Update();
    v293->Update();
    v294->Update();
    v295->Update();
    v296->Update();
    v297->Update();
    v298->Update();
    v299->Update();
    v300->Update();
    v301->Update();
    v302->Update();
    v303->Update();
    v304->Update();
    v305->Update();
    v306->Update();
    v307->Update();
    v308->Update();
    v309->Update();
    v310->Update();
    v311->Update();
    v312->Update();
    v313->Update();
    v314->Update();
    v315->Update();
    v316->Update();
    v317->Update();
    v318->Update();
    v319->Update();
    v320->Update();
    v321->Update();
    v322->Update();
    v323->Update();
    v324->Update();
    v325->Update();
    v326->Update();
    v327->Update();
    v328->Update();
    v329->Update();
    v330->Update();
    v331->Update();
    v332->Update();
    v333->Update();
    v334->Update();
    v335->Update();
    v336->Update();
    v337->Update();
    v338->Update();
    v339->Update();
    v340->Update();
    v341->Update();
    v342->Update();
    v343->Update();
    v344->Update();
    v345->Update();
    v346->Update();
    v347->Update();
    v348->Update();
    v349->Update();
    v350->Update();
    v351->Update();
    v352->Update();
    v353->Update();
    v354->Update();
    v355->Update();
    v356->Update();
    v357->Update();
    v358->Update();
    v359->Update();
    v360->Update();
    v361->Update();
    v362->Update();
    v363->Update();
    v364->Update();
    v365->Update();
    v366->Update();
    v367->Update();
    v368->Update();
    v369->Update();
    v370->Update();
    v371->Update();
    v372->Update();
    v373->Update();
    v374->Update();
    v375->Update();
    v376->Update();
    v377->Update();
    v378->Update();
    v379->Update();
    v380->Update();
    v381->Update();
    v382->Update();
    v383->Update();
    v384->Update();
    v385->Update();
    v386->Update();
    v387->Update();
    v388->Update();
    v389->Update();
    v390->Update();
    v391->Update();
    v392->Update();
    v393->Update();
    v394->Update();
    v395->Update();
    v396->Update();
    v397->Update();
    v398->Update();
    v399->Update();
    v400->Update();
    v401->Update();
    v402->Update();
    v403->Update();
    v404->Update();
    v405->Update();
    v406->Update();
    v407->Update();
    v408->Update();
    v409->Update();
    v410->Update();
    v411->Update();
    v412->Update();
    v413->Update();
    v414->Update();
    v415->Update();
    v416->Update();
    v417->Update();
    v418->Update();
    v419->Update();
    v420->Update();
    v421->Update();
    v422->Update();
    v423->Update();
    v424->Update();
    v425->Update();
    v426->Update();
    v427->Update();
    v428->Update();
    v429->Update();
    v430->Update();
    v431->Update();
    v432->Update();
    v433->Update();
    v434->Update();
    v435->Update();
    v436->Update();
    v437->Update();
    v438->Update();
    v439->Update();
    v440->Update();
    v441->Update();
    v442->Update();
    v443->Update();
    v444->Update();
    v445->Update();
    v446->Update();
    v447->Update();
    v448->Update();
    v449->Update();
    v450->Update();
    v451->Update();
    v452->Update();
    v453->Update();
    v454->Update();
    v455->Update();
    v456->Update();
    v457->Update();
    v458->Update();
    v459->Update();
    v460->Update();
    v461->Update();
    v462->Update();
    v463->Update();
    v464->Update();
    v465->Update();
    v466->Update();
    v467->Update();
    v468->Update();
    v469->Update();
    v470->Update();
    v471->Update();
    v472->Update();
    v473->Update();
    v474->Update();
    v475->Update();
    v476->Update();
    v477->Update();
    v478->Update();
    v479->Update();
    v480->Update();
    v481->Update();
    v482->Update();
    v483->Update();
    v484->Update();
    v485->Update();
    v486->Update();
  };
  return {update_fn};
}

} // anonymous namespace

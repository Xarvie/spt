#pragma once

#include "Object.h"
#include "Value.h"
#include "unordered_dense.h"
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace spt {

// 前置声明
class VM;
struct NativeClassObject;
struct NativeInstance;

// ============================================================================
// 类型 ID 系统 - 用于安全类型转换的编译时类型识别
// ============================================================================

using TypeId = const void *;

template <typename T> inline TypeId getTypeId() {
  static char marker;
  return &marker;
}

constexpr TypeId INVALID_TYPE_ID = nullptr;

// ============================================================================
// 原生属性描述符
// ============================================================================

using NativePropertyGetter = std::function<Value(VM *vm, NativeInstance *instance)>;
using NativePropertySetter = std::function<void(VM *vm, NativeInstance *instance, Value value)>;

struct NativePropertyDesc {
  std::string name;
  NativePropertyGetter getter;
  NativePropertySetter setter;
  bool isReadOnly;

  NativePropertyDesc() : isReadOnly(true) {}

  NativePropertyDesc(std::string n, NativePropertyGetter g, NativePropertySetter s = nullptr)
      : name(std::move(n)), getter(std::move(g)), setter(std::move(s)) {
    isReadOnly = !static_cast<bool>(setter);
  }
};

// ============================================================================
// 原生方法描述符
// ============================================================================

using NativeMethodFn =
    std::function<Value(VM *vm, NativeInstance *instance, int argc, Value *argv)>;

struct NativeMethodDesc {
  std::string name;
  NativeMethodFn function;
  int arity;

  NativeMethodDesc() : arity(0) {}

  NativeMethodDesc(const std::string &n, NativeMethodFn fn, int a)
      : name(n), function(std::move(fn)), arity(a) {}
};

// ============================================================================
// 原生构造函数/析构函数
// ============================================================================

using NativeConstructorFn = std::function<void *(VM *vm, int argc, Value *argv)>;
using NativeDestructorFn = std::function<void(void *ptr)>;

// ============================================================================
// 所有权模式
// ============================================================================

enum class OwnershipMode : uint8_t { OwnedByVM, OwnedExternally };

// ============================================================================
// NativeClassObject - 代表脚本系统中的一个 C++ 类
// ============================================================================

struct NativeClassObject : GCObject {
  std::string name;
  TypeId typeId = INVALID_TYPE_ID;
  NativeClassObject *baseClass;

  NativeConstructorFn constructor;
  NativeDestructorFn destructor;
  OwnershipMode defaultOwnership;

  StringMap<NativeMethodDesc> methods;
  StringMap<NativePropertyDesc> properties;
  StringMap<Value> statics;

  size_t instanceDataSize = 0;
  bool allowInheritance = true;
  std::string documentation;

  NativeClassObject() : baseClass(nullptr), defaultOwnership(OwnershipMode::OwnedByVM) {
    type = ValueType::NativeClass;
  }

  bool isOrDerivedFrom(TypeId targetTypeId) const {
    if (typeId == targetTypeId)
      return true;
    if (baseClass)
      return baseClass->isOrDerivedFrom(targetTypeId);
    return false;
  }

  const NativeMethodDesc *findMethod(StringObject *methodName) const {
    auto it = methods.find(methodName);
    if (it != methods.end())
      return &it->second;
    if (baseClass)
      return baseClass->findMethod(methodName);
    return nullptr;
  }

  const NativePropertyDesc *findProperty(StringObject *propName) const {
    auto it = properties.find(propName);
    if (it != properties.end())
      return &it->second;
    if (baseClass)
      return baseClass->findProperty(propName);
    return nullptr;
  }

  bool hasConstructor() const { return static_cast<bool>(constructor); }

  bool hasDestructor() const { return static_cast<bool>(destructor); }
};

// ============================================================================
// NativeInstance - 包装一个 C++ 对象实例
// ============================================================================

struct NativeInstance : GCObject {
  NativeClassObject *nativeClass;
  void *data;
  OwnershipMode ownership;
  bool isDestroyed = false;
  StringMap<Value> fields;

  NativeInstance() : nativeClass(nullptr), data(nullptr), ownership(OwnershipMode::OwnedByVM) {
    type = ValueType::NativeObject;
  }

  template <typename T> T *as() { return static_cast<T *>(data); }

  template <typename T> const T *as() const { return static_cast<const T *>(data); }

  template <typename T> T *safeCast() {
    if (!nativeClass)
      return nullptr;
    if (!nativeClass->isOrDerivedFrom(getTypeId<T>()))
      return nullptr;
    return static_cast<T *>(data);
  }

  template <typename T> const T *safeCast() const {
    if (!nativeClass)
      return nullptr;
    if (!nativeClass->isOrDerivedFrom(getTypeId<T>()))
      return nullptr;
    return static_cast<const T *>(data);
  }

  void destroy() {
    if (isDestroyed || !data)
      return;

    switch (ownership) {
    case OwnershipMode::OwnedByVM:
      // VM 拥有，需要调用析构函数
      if (nativeClass && nativeClass->destructor) {
        nativeClass->destructor(data);
      }
      break;

    case OwnershipMode::OwnedExternally:
      // 外部拥有，不做任何事
      break;
    }

    isDestroyed = true;
    data = nullptr;
  }

  bool isValid() const { return data != nullptr && !isDestroyed; }

  Value getField(StringObject *name) const {
    auto it = fields.find(name);
    return (it != fields.end()) ? it->second : Value::nil();
  }

  void setField(StringObject *name, const Value &value) { fields[name] = value; }

  bool hasField(StringObject *name) const { return fields.find(name) != fields.end(); }
};

// ============================================================================
// 值转换辅助函数
// ============================================================================

namespace native_detail {

template <typename T> struct ValueConverter {
  static T from(const Value &v);
  static Value to(VM *vm, const T &value);
};

template <> struct ValueConverter<int64_t> {
  static int64_t from(const Value &v) { return v.isInt() ? v.asInt() : 0; }

  static Value to(VM *vm, int64_t value) { return Value::integer(value); }
};

template <> struct ValueConverter<int> {
  static int from(const Value &v) { return static_cast<int>(v.isInt() ? v.asInt() : 0); }

  static Value to(VM *vm, int value) { return Value::integer(value); }
};

template <> struct ValueConverter<double> {
  static double from(const Value &v) { return v.isNumber() ? v.asNumber() : 0.0; }

  static Value to(VM *vm, double value) { return Value::number(value); }
};

template <> struct ValueConverter<float> {
  static float from(const Value &v) {
    return static_cast<float>(v.isNumber() ? v.asNumber() : 0.0);
  }

  static Value to(VM *vm, float value) { return Value::number(value); }
};

template <> struct ValueConverter<bool> {
  static bool from(const Value &v) { return v.isBool() ? v.asBool() : false; }

  static Value to(VM *vm, bool value) { return Value::boolean(value); }
};

template <> struct ValueConverter<std::string> {
  static std::string from(const Value &v) {
    if (v.isString()) {
      return v.asString()->str();
    }
    return "";
  }

  static Value to(VM *vm, const std::string &value);
};

template <> struct ValueConverter<Value> {
  static Value from(const Value &v) { return v; }

  static Value to(VM *vm, const Value &value) { return value; }
};

} // namespace native_detail

// ============================================================================
// NativeBindingRegistry - 全局原生类注册表
// ============================================================================

class NativeBindingRegistry {
public:
  static NativeBindingRegistry &instance() {
    static NativeBindingRegistry registry;
    return registry;
  }

  void registerClass(TypeId typeId, NativeClassObject *nativeClass) {
    typeToClass_[typeId] = nativeClass;
  }

  NativeClassObject *findClass(TypeId typeId) const {
    auto it = typeToClass_.find(typeId);
    return (it != typeToClass_.end()) ? it->second : nullptr;
  }

private:
  NativeBindingRegistry() = default;
  ankerl::unordered_dense::map<TypeId, NativeClassObject *> typeToClass_;
};

// ============================================================================
// NativeClassBuilder - 流式 API 构建原生类
// ============================================================================

template <typename T> class NativeClassBuilder {
public:
  NativeClassBuilder(VM *vm, const std::string &name);

  // === 基类设置 ===
  template <typename Base> NativeClassBuilder &extends() {
    NativeClassObject *baseClass = NativeBindingRegistry::instance().findClass(getTypeId<Base>());
    if (baseClass) {
      class_->baseClass = baseClass;
    }
    return *this;
  }

  // === 构造函数注册 ===
  template <typename... Args> NativeClassBuilder &constructor() {
    class_->constructor = [](VM *vm, int argc, Value *argv) -> void * {
      if (argc != sizeof...(Args)) {
        return nullptr;
      }
      return constructWithArgs<Args...>(argv, std::index_sequence_for<Args...>{});
    };
    return *this;
  }

  NativeClassBuilder &constructor(NativeConstructorFn fn) {
    class_->constructor = std::move(fn);
    return *this;
  }

  // === 析构函数注册 ===
  NativeClassBuilder &destructor(NativeDestructorFn fn) {
    class_->destructor = std::move(fn);
    return *this;
  }

  NativeClassBuilder &defaultDestructor() {
    class_->destructor = [](void *ptr) { delete static_cast<T *>(ptr); };
    return *this;
  }

  // === 方法注册 ===
  NativeClassBuilder &method(StringObject *name, NativeMethodFn fn, int arity = -1) {
    class_->methods[name] = NativeMethodDesc(name->str(), std::move(fn), arity);
    return *this;
  }

  NativeClassBuilder &method(const std::string &name, NativeMethodFn fn, int arity = -1);

  template <auto Method> NativeClassBuilder &method(const std::string &name) {
    using Traits = MethodTraits_<decltype(Method)>;
    constexpr size_t arity = Traits::Arity;

    return method(
        name,
        [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
          if (!inst || !inst->isValid()) {
            return Value::nil();
          }
          T *obj = inst->as<T>();
          return invokeMethod_<Method, Traits>(vm, obj, argv, std::make_index_sequence<arity>{});
        },
        static_cast<int>(arity));
  }

private:
  // 从成员函数指针提取返回类型和参数类型
  template <typename MemFnPtr> struct MethodTraits_;

  template <typename RetT, typename... Args> struct MethodTraits_<RetT (T::*)(Args...)> {
    using ReturnType = RetT;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t Arity = sizeof...(Args);
  };

  template <typename RetT, typename... Args> struct MethodTraits_<RetT (T::*)(Args...) const> {
    using ReturnType = RetT;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t Arity = sizeof...(Args);
  };

  // 展开参数元组并调用成员函数
  template <auto Method, typename Traits, size_t... Is>
  static Value invokeMethod_(VM *vm, T *obj, Value *argv, std::index_sequence<Is...>) {
    using RetT = typename Traits::ReturnType;
    using ArgsTuple = typename Traits::ArgsTuple;

    if constexpr (std::is_void_v<RetT>) {
      (obj->*Method)(
          native_detail::ValueConverter<std::tuple_element_t<Is, ArgsTuple>>::from(argv[Is])...);
      return Value::nil();
    } else {
      RetT result = (obj->*Method)(
          native_detail::ValueConverter<std::tuple_element_t<Is, ArgsTuple>>::from(argv[Is])...);
      return native_detail::ValueConverter<RetT>::to(vm, result);
    }
  }

public:
  // === 属性注册 ===
  NativeClassBuilder &propertyReadOnly(const std::string &name, NativePropertyGetter getter);
  NativeClassBuilder &property(const std::string &name, NativePropertyGetter getter,
                               NativePropertySetter setter);

  template <typename MemberT, MemberT T::*Member>
  NativeClassBuilder &memberProperty(const std::string &name);

  template <typename MemberT, MemberT T::*Member>
  NativeClassBuilder &memberPropertyReadOnly(const std::string &name);

  // === 静态成员 ===
  NativeClassBuilder &staticMethod(const std::string &name, NativeFn fn, int arity = -1);
  NativeClassBuilder &staticValue(const std::string &name, Value value);

  // === 配置 ===
  NativeClassBuilder &ownership(OwnershipMode mode) {
    class_->defaultOwnership = mode;
    return *this;
  }

  NativeClassBuilder &disallowInheritance() {
    class_->allowInheritance = false;
    return *this;
  }

  // === 完成构建 ===
  NativeClassObject *build();

  NativeClassObject *get() { return class_; }

private:
  template <typename... Args, size_t... Is>
  static T *constructWithArgs(Value *argv, std::index_sequence<Is...>) {
    return new T(native_detail::ValueConverter<Args>::from(argv[Is])...);
  }

  VM *vm_;
  NativeClassObject *class_;
};

// ============================================================================
// 实用函数声明
// ============================================================================

template <typename T>
NativeInstance *wrapNativeObject(VM *vm, T *obj,
                                 OwnershipMode ownership = OwnershipMode::OwnedExternally);

template <typename T, typename... Args> NativeInstance *createNativeObject(VM *vm, Args &&...args);

template <typename T> T *getNativeObject(const Value &value);
template <typename T> bool isNativeObject(const Value &value);

// ============================================================================
// 便捷宏
// ============================================================================

#define SPT_NATIVE_METHOD(ClassName, MethodName)                                                   \
  [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {                               \
    if (!inst || !inst->isValid()) {                                                               \
      return Value::nil();                                                                         \
    }                                                                                              \
    ClassName *self = inst->as<ClassName>();                                                       \
    return self->MethodName(vm, argc, argv);                                                       \
  }

#define SPT_GETTER(ClassName, MemberName, ToValueFn)                                               \
  [](VM *vm, NativeInstance *inst) -> Value {                                                      \
    if (!inst || !inst->isValid()) {                                                               \
      return Value::nil();                                                                         \
    }                                                                                              \
    ClassName *self = inst->as<ClassName>();                                                       \
    return ToValueFn(vm, self->MemberName);                                                        \
  }

#define SPT_SETTER(ClassName, MemberName, FromValueFn)                                             \
  [](VM *vm, NativeInstance *inst, Value value) {                                                  \
    if (!inst || !inst->isValid()) {                                                               \
      return;                                                                                      \
    }                                                                                              \
    ClassName *self = inst->as<ClassName>();                                                       \
    self->MemberName = FromValueFn(value);                                                         \
  }

} // namespace spt

// ============================================================================
// 模板实现 - 必须在头文件中
// ============================================================================

#include "VM.h"

namespace spt {

// ValueConverter<std::string>::to 实现
inline Value native_detail::ValueConverter<std::string>::to(VM *vm, const std::string &value) {
  return Value::object(vm->allocateString(value));
}

// NativeClassBuilder 实现
template <typename T>
NativeClassBuilder<T>::NativeClassBuilder(VM *vm, const std::string &name) : vm_(vm) {
  class_ = vm_->allocateNativeClass(name);
  class_->typeId = getTypeId<T>();
  class_->instanceDataSize = sizeof(T);
  class_->destructor = [](void *ptr) { delete static_cast<T *>(ptr); };
}

template <typename T>
NativeClassBuilder<T> &NativeClassBuilder<T>::method(const std::string &name, NativeMethodFn fn,
                                                     int arity) {
  StringObject *nameStr = vm_->allocateString(name);
  class_->methods[nameStr] = NativeMethodDesc(name, std::move(fn), arity);
  return *this;
}

template <typename T>
NativeClassBuilder<T> &NativeClassBuilder<T>::propertyReadOnly(const std::string &name,
                                                               NativePropertyGetter getter) {
  StringObject *nameStr = vm_->allocateString(name);
  class_->properties[nameStr] = NativePropertyDesc(name, std::move(getter), nullptr);
  return *this;
}

template <typename T>
NativeClassBuilder<T> &NativeClassBuilder<T>::property(const std::string &name,
                                                       NativePropertyGetter getter,
                                                       NativePropertySetter setter) {
  StringObject *nameStr = vm_->allocateString(name);
  class_->properties[nameStr] = NativePropertyDesc(name, std::move(getter), std::move(setter));
  return *this;
}

template <typename T>
template <typename MemberT, MemberT T::*Member>
NativeClassBuilder<T> &NativeClassBuilder<T>::memberProperty(const std::string &name) {
  StringObject *nameStr = vm_->allocateString(name);
  class_->properties[nameStr] = NativePropertyDesc(
      name,
      [](VM *vm, NativeInstance *inst) -> Value {
        if (!inst || !inst->isValid()) {
          return Value::nil();
        }
        T *obj = inst->as<T>();
        return native_detail::ValueConverter<MemberT>::to(vm, obj->*Member);
      },
      [](VM *vm, NativeInstance *inst, Value value) {
        if (!inst || !inst->isValid()) {
          return;
        }
        T *obj = inst->as<T>();
        obj->*Member = native_detail::ValueConverter<MemberT>::from(value);
      });
  return *this;
}

template <typename T>
template <typename MemberT, MemberT T::*Member>
NativeClassBuilder<T> &NativeClassBuilder<T>::memberPropertyReadOnly(const std::string &name) {
  StringObject *nameStr = vm_->allocateString(name);
  class_->properties[nameStr] = NativePropertyDesc(
      name,
      [](VM *vm, NativeInstance *inst) -> Value {
        if (!inst || !inst->isValid()) {
          return Value::nil();
        }
        T *obj = inst->as<T>();
        return native_detail::ValueConverter<MemberT>::to(vm, obj->*Member);
      },
      nullptr);
  return *this;
}

template <typename T>
NativeClassBuilder<T> &NativeClassBuilder<T>::staticMethod(const std::string &name, NativeFn fn,
                                                           int arity) {
  NativeFunction *native = vm_->gc().allocate<NativeFunction>();
  native->name = name;
  native->function = std::move(fn);
  native->arity = arity;
  StringObject *nameStr = vm_->allocateString(name);
  class_->statics[nameStr] = Value::object(native);
  return *this;
}

template <typename T>
NativeClassBuilder<T> &NativeClassBuilder<T>::staticValue(const std::string &name, Value value) {
  StringObject *nameStr = vm_->allocateString(name);
  class_->statics[nameStr] = value;
  return *this;
}

template <typename T> NativeClassObject *NativeClassBuilder<T>::build() {
  NativeBindingRegistry::instance().registerClass(class_->typeId, class_);
  vm_->defineGlobal(class_->name, Value::object(class_));
  return class_;
}

// 模板实用函数实现
template <typename T> NativeInstance *wrapNativeObject(VM *vm, T *obj, OwnershipMode ownership) {
  NativeClassObject *nativeClass = NativeBindingRegistry::instance().findClass(getTypeId<T>());
  if (!nativeClass) {
    return nullptr;
  }
  NativeInstance *instance = vm->allocateNativeInstance(nativeClass);
  instance->data = obj;
  instance->ownership = ownership;
  return instance;
}

template <typename T, typename... Args> NativeInstance *createNativeObject(VM *vm, Args &&...args) {
  NativeClassObject *nativeClass = NativeBindingRegistry::instance().findClass(getTypeId<T>());
  if (!nativeClass) {
    return nullptr;
  }
  T *obj = new T(std::forward<Args>(args)...);
  NativeInstance *instance = vm->allocateNativeInstance(nativeClass);
  instance->data = obj;
  instance->ownership = OwnershipMode::OwnedByVM;
  return instance;
}

template <typename T> T *getNativeObject(const Value &value) {
  if (!value.isNativeInstance())
    return nullptr;
  NativeInstance *instance = static_cast<NativeInstance *>(value.asGC());
  return instance->safeCast<T>();
}

template <typename T> bool isNativeObject(const Value &value) {
  if (!value.isNativeInstance())
    return false;
  NativeInstance *instance = static_cast<NativeInstance *>(value.asGC());
  if (!instance->nativeClass)
    return false;
  return instance->nativeClass->isOrDerivedFrom(getTypeId<T>());
}

} // namespace spt
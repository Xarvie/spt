#pragma once

#include "Object.h"
#include "Value.h"
#include "unordered_dense.h"
#include <functional>
#include <memory>
#include <string>
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

enum class OwnershipMode : uint8_t { OwnedByVM, OwnedExternally, Shared };

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

  ankerl::unordered_dense::map<std::string, NativeMethodDesc> methods;
  ankerl::unordered_dense::map<std::string, NativePropertyDesc> properties;
  ankerl::unordered_dense::map<std::string, Value> statics;

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

  const NativeMethodDesc *findMethod(const std::string &methodName) const {
    auto it = methods.find(methodName);
    if (it != methods.end())
      return &it->second;
    if (baseClass)
      return baseClass->findMethod(methodName);
    return nullptr;
  }

  const NativePropertyDesc *findProperty(const std::string &propName) const {
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
  ankerl::unordered_dense::map<std::string, Value> fields;

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
    if (ownership == OwnershipMode::OwnedByVM && nativeClass && nativeClass->destructor) {
      nativeClass->destructor(data);
    }
    isDestroyed = true;
    data = nullptr;
  }

  bool isValid() const { return data != nullptr && !isDestroyed; }

  Value getField(const std::string &name) const {
    auto it = fields.find(name);
    return (it != fields.end()) ? it->second : Value::nil();
  }

  void setField(const std::string &name, const Value &value) { fields[name] = value; }

  bool hasField(const std::string &name) const { return fields.find(name) != fields.end(); }
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

  static Value to(VM *vm, float value) { return Value::number(static_cast<double>(value)); }
};

template <> struct ValueConverter<bool> {
  static bool from(const Value &v) { return v.isTruthy(); }

  static Value to(VM *vm, bool value) { return Value::boolean(value); }
};

template <> struct ValueConverter<std::string> {
  static inline std::string from(const Value &v) {
    if (!v.isString())
      return "";
    return static_cast<StringObject *>(v.asGC())->data;
  }

  static Value to(VM *vm, const std::string &value);
};

} // namespace native_detail

// ============================================================================
// NativeBindingRegistry - 原生类的中央注册表
// ============================================================================

class NativeBindingRegistry {
public:
  static NativeBindingRegistry &instance() {
    static NativeBindingRegistry registry;
    return registry;
  }

  void registerClass(TypeId typeId, NativeClassObject *klass) {
    typeIdToClass_[typeId] = klass;
    nameToClass_[klass->name] = klass;
  }

  NativeClassObject *findClass(TypeId typeId) const {
    auto it = typeIdToClass_.find(typeId);
    return (it != typeIdToClass_.end()) ? it->second : nullptr;
  }

  NativeClassObject *findClassByName(const std::string &name) const {
    auto it = nameToClass_.find(name);
    return (it != nameToClass_.end()) ? it->second : nullptr;
  }

  void clear() {
    typeIdToClass_.clear();
    nameToClass_.clear();
  }

private:
  NativeBindingRegistry() = default;
  ankerl::unordered_dense::map<TypeId, NativeClassObject *> typeIdToClass_;
  ankerl::unordered_dense::map<std::string, NativeClassObject *> nameToClass_;
};

// ============================================================================
// NativeClassBuilder - 用于注册原生类的流式 API
// ============================================================================

template <typename T> class NativeClassBuilder {
public:
  explicit NativeClassBuilder(VM *vm, const std::string &name);

  NativeClassBuilder &inherits(NativeClassObject *base) {
    class_->baseClass = base;
    return *this;
  }

  NativeClassBuilder &doc(const std::string &documentation) {
    class_->documentation = documentation;
    return *this;
  }

  // === 构造函数注册 ===

  template <typename = std::enable_if_t<std::is_default_constructible_v<T>>>
  NativeClassBuilder &defaultConstructor() {
    class_->constructor = [](VM *vm, int argc, Value *argv) -> void * { return new T(); };
    return *this;
  }

  NativeClassBuilder &constructor(NativeConstructorFn ctor) {
    class_->constructor = std::move(ctor);
    return *this;
  }

  template <typename... Args> NativeClassBuilder &constructorArgs() {
    class_->constructor = [](VM *vm, int argc, Value *argv) -> void * {
      if (argc < static_cast<int>(sizeof...(Args))) {
        return nullptr;
      }
      return constructWithArgs<Args...>(argv, std::index_sequence_for<Args...>{});
    };
    return *this;
  }

  NativeClassBuilder &destructor(NativeDestructorFn dtor) {
    class_->destructor = std::move(dtor);
    return *this;
  }

  NativeClassBuilder &defaultDestructor() {
    class_->destructor = [](void *ptr) { delete static_cast<T *>(ptr); };
    return *this;
  }

  // === 方法注册 ===

  NativeClassBuilder &method(const std::string &name, NativeMethodFn fn, int arity = -1) {
    class_->methods[name] = NativeMethodDesc(name, std::move(fn), arity);
    return *this;
  }

  // Bug #8 修复: 添加有效性检查
  template <Value (T::*Method)(VM *)> NativeClassBuilder &method(const std::string &name) {
    class_->methods[name] = NativeMethodDesc(
        name,
        [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
          if (!inst || !inst->isValid()) {
            return Value::nil();
          }
          T *obj = inst->as<T>();
          return (obj->*Method)(vm);
        },
        0);
    return *this;
  }

  // Bug #8 修复: 添加有效性检查
  template <void (T::*Method)()> NativeClassBuilder &methodVoid(const std::string &name) {
    class_->methods[name] = NativeMethodDesc(
        name,
        [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
          if (!inst || !inst->isValid()) {
            return Value::nil();
          }
          T *obj = inst->as<T>();
          (obj->*Method)();
          return Value::nil();
        },
        0);
    return *this;
  }

  // === 属性注册 ===

  NativeClassBuilder &propertyReadOnly(const std::string &name, NativePropertyGetter getter) {
    class_->properties[name] = NativePropertyDesc(name, std::move(getter), nullptr);
    return *this;
  }

  NativeClassBuilder &property(const std::string &name, NativePropertyGetter getter,
                               NativePropertySetter setter) {
    class_->properties[name] = NativePropertyDesc(name, std::move(getter), std::move(setter));
    return *this;
  }

  // Bug #10 修复: 添加有效性检查
  template <typename MemberT, MemberT T::*Member>
  NativeClassBuilder &memberProperty(const std::string &name) {
    class_->properties[name] = NativePropertyDesc(
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

  // Bug #10 修复: 添加有效性检查
  template <typename MemberT, MemberT T::*Member>
  NativeClassBuilder &memberPropertyReadOnly(const std::string &name) {
    class_->properties[name] = NativePropertyDesc(
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

  // === 静态成员 ===

  NativeClassBuilder &staticMethod(const std::string &name, NativeFn fn, int arity = -1);

  NativeClassBuilder &staticValue(const std::string &name, Value value) {
    class_->statics[name] = value;
    return *this;
  }

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
NativeClassBuilder<T> &NativeClassBuilder<T>::staticMethod(const std::string &name, NativeFn fn,
                                                           int arity) {
  NativeFunction *native = vm_->gc().allocate<NativeFunction>();
  native->name = name;
  native->function = std::move(fn);
  native->arity = arity;
  class_->statics[name] = Value::object(native);
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
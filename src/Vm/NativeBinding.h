#pragma once

#include "Object.h"
#include "Value.h"
#include "unordered_dense.h"
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace spt {

// 前向声明
class VM;
struct NativeClassObject;
struct NativeInstance;

// ============================================================================
// 类型 ID 系统 - 用于安全类型转换的编译时类型识别
// ============================================================================

// 每个 C++ 类型根据模板实例化的地址获取唯一的 TypeId
using TypeId = const void *;

// 获取任意类型 T 的唯一类型 ID
template <typename T> inline TypeId getTypeId() {
  static char marker;
  return &marker;
}

// 无效/空类型 ID
constexpr TypeId INVALID_TYPE_ID = nullptr;

// ============================================================================
// 原生属性描述符 (Native Property Descriptor)
// ============================================================================

// 属性读取器 (getter): (vm, instance) -> value
using NativePropertyGetter = std::function<Value(VM *vm, NativeInstance *instance)>;

// 属性设置器 (setter): (vm, instance, value) -> void
using NativePropertySetter = std::function<void(VM *vm, NativeInstance *instance, Value value)>;

struct NativePropertyDesc {
  std::string name;
  NativePropertyGetter getter;
  NativePropertySetter setter; // 对于只读属性为 nullptr
  bool isReadOnly;

  NativePropertyDesc() : isReadOnly(true) {}

  NativePropertyDesc(const std::string &n, NativePropertyGetter g, NativePropertySetter s = nullptr)
      : name(n), getter(std::move(g)), setter(std::move(s)), isReadOnly(s == nullptr) {}
};

// ============================================================================
// 原生方法描述符 (Native Method Descriptor)
// ============================================================================

// 方法签名: (vm, instance, argc, argv) -> result
using NativeMethodFn =
    std::function<Value(VM *vm, NativeInstance *instance, int argc, Value *argv)>;

// 静态方法签名：与 NativeFn 相同
// using NativeFn = std::function<Value(VM *vm, Value receiver, int argc, Value *args)>;

struct NativeMethodDesc {
  std::string name;
  NativeMethodFn function;
  int arity; // 参数个数，-1 表示变长参数

  NativeMethodDesc() : arity(0) {}

  NativeMethodDesc(const std::string &n, NativeMethodFn fn, int a)
      : name(n), function(std::move(fn)), arity(a) {}
};

// ============================================================================
// 原生构造函数/析构函数
// ============================================================================

// 构造函数: (vm, argc, argv) -> 指向已分配对象的原始指针
// 失败时返回 nullptr
using NativeConstructorFn = std::function<void *(VM *vm, int argc, Value *argv)>;

// 析构函数: (pointer) -> void
// 当 GC 回收 NativeInstance 或对象被显式销毁时调用
using NativeDestructorFn = std::function<void(void *ptr)>;

// ============================================================================
// 所有权模式 - 谁负责 C++ 对象的生命周期？
// ============================================================================

enum class OwnershipMode : uint8_t {
  // VM 拥有该对象 - 在 GC 回收时调用析构函数
  OwnedByVM,

  // 外部代码拥有该对象 - GC 不会调用析构函数
  OwnedExternally,

  // 共享所有权 - 引用计数（未来扩展）
  Shared
};

// ============================================================================
// NativeClassObject - 代表脚本系统中的一个 C++ 类
// ============================================================================

struct NativeClassObject : GCObject {
  // === 标识 ===
  std::string name;                // 类名 (例如 "Vector3", "Entity")
  TypeId typeId = INVALID_TYPE_ID; // 用于安全类型转换的唯一类型标识符
  NativeClassObject *baseClass;    // 继承的父类 (可以为 nullptr)

  // === 生命周期 ===
  NativeConstructorFn constructor; // 对象创建的回调
  NativeDestructorFn destructor;   // 清理对象的回调
  OwnershipMode defaultOwnership;  // 新实例的默认所有权模式

  // === 成员 ===
  ankerl::unordered_dense::map<std::string, NativeMethodDesc> methods;
  ankerl::unordered_dense::map<std::string, NativePropertyDesc> properties;
  ankerl::unordered_dense::map<std::string, Value> statics; // 静态成员/方法

  // === 元数据 ===
  size_t instanceDataSize = 0;  // 包装类型的 sizeof(T)
  bool allowInheritance = true; // 是否允许脚本类继承此类？
  std::string documentation;    // 可选的文档字符串

  // 构造函数
  NativeClassObject() : baseClass(nullptr), defaultOwnership(OwnershipMode::OwnedByVM) {
    type = ValueType::NativeClass;
  }

  // 检查此类是否为指定类型或派生自该类型
  bool isOrDerivedFrom(TypeId targetTypeId) const {
    if (typeId == targetTypeId)
      return true;
    if (baseClass)
      return baseClass->isOrDerivedFrom(targetTypeId);
    return false;
  }

  // 查找方法，会搜索继承链
  const NativeMethodDesc *findMethod(const std::string &methodName) const {
    auto it = methods.find(methodName);
    if (it != methods.end())
      return &it->second;
    if (baseClass)
      return baseClass->findMethod(methodName);
    return nullptr;
  }

  // 查找属性，会搜索继承链
  const NativePropertyDesc *findProperty(const std::string &propName) const {
    auto it = properties.find(propName);
    if (it != properties.end())
      return &it->second;
    if (baseClass)
      return baseClass->findProperty(propName);
    return nullptr;
  }

  // 检查是否有有效的构造函数
  bool hasConstructor() const { return static_cast<bool>(constructor); }

  // 检查是否有析构函数
  bool hasDestructor() const { return static_cast<bool>(destructor); }
};

// ============================================================================
// NativeInstance - 包装一个 C++ 对象实例
// ============================================================================

struct NativeInstance : GCObject {
  // === 核心数据 ===
  NativeClassObject *nativeClass; // 该实例所属的类
  void *data;                     // 指向实际 C++ 对象的指针
  OwnershipMode ownership;        // 谁拥有该数据指针？

  // === 状态 ===
  bool isDestroyed = false; // 析构函数是否已被调用？

  // === 可选的脚本侧字段 ===
  // 允许从脚本代码添加额外字段（类似于 Instance 对象）
  ankerl::unordered_dense::map<std::string, Value> fields;

  // 构造函数
  NativeInstance() : nativeClass(nullptr), data(nullptr), ownership(OwnershipMode::OwnedByVM) {
    type = ValueType::NativeObject;
  }

  // 获取原始数据指针并转换为类型 T
  // 警告：调用者必须先验证类型安全性！
  template <typename T> T *as() { return static_cast<T *>(data); }

  template <typename T> const T *as() const { return static_cast<const T *>(data); }

  // 带有运行时检查的安全性类型转换
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

  // 调用析构函数并标记为已销毁
  void destroy() {
    if (isDestroyed || !data)
      return;
    if (ownership == OwnershipMode::OwnedByVM && nativeClass && nativeClass->destructor) {
      nativeClass->destructor(data);
    }
    isDestroyed = true;
    data = nullptr;
  }

  // 检查该实例是否有效
  bool isValid() const { return data != nullptr && !isDestroyed; }

  // 获取字段（脚本侧扩展）
  Value getField(const std::string &name) const {
    auto it = fields.find(name);
    return (it != fields.end()) ? it->second : Value::nil();
  }

  // 设置字段（脚本侧扩展）
  void setField(const std::string &name, const Value &value) { fields[name] = value; }

  // 检查字段是否存在
  bool hasField(const std::string &name) const { return fields.find(name) != fields.end(); }
};

// ============================================================================
// 值转换辅助函数
// ============================================================================

namespace native_detail {

// 将 Value 转换为 C++ 类型
template <typename T> struct ValueConverter {
  static T from(const Value &v);
  static Value to(VM *vm, const T &value);
};

// 常见类型的特化实现
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

// 字符串转换 - 内联实现
template <> struct ValueConverter<std::string> {
  static inline std::string from(const Value &v) {
    if (!v.isString())
      return "";
    return static_cast<StringObject *>(v.asGC())->data;
  }

  // to() 需要 VM，在包含 VM.h 之后定义
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

  // 通过 TypeId 注册类
  void registerClass(TypeId typeId, NativeClassObject *klass) {
    typeIdToClass_[typeId] = klass;
    nameToClass_[klass->name] = klass;
  }

  // 通过 TypeId 查找类
  NativeClassObject *findClass(TypeId typeId) const {
    auto it = typeIdToClass_.find(typeId);
    return (it != typeIdToClass_.end()) ? it->second : nullptr;
  }

  // 通过名称查找类
  NativeClassObject *findClassByName(const std::string &name) const {
    auto it = nameToClass_.find(name);
    return (it != nameToClass_.end()) ? it->second : nullptr;
  }

  // 清除所有注册信息
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
  // 构造函数 - 在下方包含 VM.h 后内联定义
  explicit NativeClassBuilder(VM *vm, const std::string &name);

  // 设置继承的父类
  NativeClassBuilder &inherits(NativeClassObject *base) {
    class_->baseClass = base;
    return *this;
  }

  // 设置文档字符串
  NativeClassBuilder &doc(const std::string &documentation) {
    class_->documentation = documentation;
    return *this;
  }

  // === 构造函数注册 ===

  // 默认构造函数 (T 必须是可默认构造的)
  template <typename = std::enable_if_t<std::is_default_constructible_v<T>>>
  NativeClassBuilder &defaultConstructor() {
    class_->constructor = [](VM *vm, int argc, Value *argv) -> void * { return new T(); };
    return *this;
  }

  // 带有参数转换的自定义构造函数
  NativeClassBuilder &constructor(NativeConstructorFn ctor) {
    class_->constructor = std::move(ctor);
    return *this;
  }

  // 带有类型化参数的构造函数（便捷封装）
  template <typename... Args> NativeClassBuilder &constructorArgs() {
    class_->constructor = [](VM *vm, int argc, Value *argv) -> void * {
      if (argc < static_cast<int>(sizeof...(Args))) {
        return nullptr;
      }
      return constructWithArgs<Args...>(argv, std::index_sequence_for<Args...>{});
    };
    return *this;
  }

  // 自定义析构函数
  NativeClassBuilder &destructor(NativeDestructorFn dtor) {
    class_->destructor = std::move(dtor);
    return *this;
  }

  // 默认析构函数 (调用 delete)
  NativeClassBuilder &defaultDestructor() {
    class_->destructor = [](void *ptr) { delete static_cast<T *>(ptr); };
    return *this;
  }

  // === 方法注册 ===

  // 使用原始签名注册方法
  NativeClassBuilder &method(const std::string &name, NativeMethodFn fn, int arity = -1) {
    class_->methods[name] = NativeMethodDesc(name, std::move(fn), arity);
    return *this;
  }

  // 注册成员函数指针 (无参数，返回 Value)
  template <Value (T::*Method)(VM *)> NativeClassBuilder &method(const std::string &name) {
    class_->methods[name] = NativeMethodDesc(
        name,
        [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
          T *obj = inst->as<T>();
          return (obj->*Method)(vm);
        },
        0);
    return *this;
  }

  // 注册 void 成员函数 (无参数)
  template <void (T::*Method)()> NativeClassBuilder &methodVoid(const std::string &name) {
    class_->methods[name] = NativeMethodDesc(
        name,
        [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
          T *obj = inst->as<T>();
          (obj->*Method)();
          return Value::nil();
        },
        0);
    return *this;
  }

  // === 属性注册 ===

  // 注册只读属性
  NativeClassBuilder &propertyReadOnly(const std::string &name, NativePropertyGetter getter) {
    class_->properties[name] = NativePropertyDesc(name, std::move(getter), nullptr);
    return *this;
  }

  // 注册读写属性
  NativeClassBuilder &property(const std::string &name, NativePropertyGetter getter,
                               NativePropertySetter setter) {
    class_->properties[name] = NativePropertyDesc(name, std::move(getter), std::move(setter));
    return *this;
  }

  // 将成员变量注册为属性
  template <typename MemberT, MemberT T::*Member>
  NativeClassBuilder &memberProperty(const std::string &name) {
    class_->properties[name] = NativePropertyDesc(
        name,
        // 读取器 (Getter)
        [](VM *vm, NativeInstance *inst) -> Value {
          T *obj = inst->as<T>();
          return native_detail::ValueConverter<MemberT>::to(vm, obj->*Member);
        },
        // 设置器 (Setter)
        [](VM *vm, NativeInstance *inst, Value value) {
          T *obj = inst->as<T>();
          obj->*Member = native_detail::ValueConverter<MemberT>::from(value);
        });
    return *this;
  }

  // 注册只读成员变量
  template <typename MemberT, MemberT T::*Member>
  NativeClassBuilder &memberPropertyReadOnly(const std::string &name) {
    class_->properties[name] = NativePropertyDesc(
        name,
        // 读取器 (Getter)
        [](VM *vm, NativeInstance *inst) -> Value {
          T *obj = inst->as<T>();
          return native_detail::ValueConverter<MemberT>::to(vm, obj->*Member);
        },
        nullptr);
    return *this;
  }

  // === 静态成员 ===

  // 注册静态方法 (使用 NativeFn 签名) - 在下方内联定义
  NativeClassBuilder &staticMethod(const std::string &name, NativeFn fn, int arity = -1);

  // 注册静态值
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

  // 在 VM 中注册该类并返回它 - 在下方内联定义
  NativeClassObject *build();

  // 获取类对象但不注册（用于延迟注册）
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
// 实用函数 - 声明
// ============================================================================

// 创建一个包装现有 C++ 对象的 NativeInstance
// 除非指定 OwnedByVM，否则调用者保留所有权
template <typename T>
NativeInstance *wrapNativeObject(VM *vm, T *obj,
                                 OwnershipMode ownership = OwnershipMode::OwnedExternally);

// 通过构造一个新的 C++ 对象来创建 NativeInstance
// VM 将拥有该对象的所有权
template <typename T, typename... Args> NativeInstance *createNativeObject(VM *vm, Args &&...args);

// 从 Value 中提取 C++ 指针，带有类型安全检查
template <typename T> T *getNativeObject(const Value &value);

// 检查 Value 是否包含指定类型的原生对象
template <typename T> bool isNativeObject(const Value &value);

// ============================================================================
// 常用模式的便捷宏
// ============================================================================

// 定义带有类型化 this 指针的原生方法的宏
#define SPT_NATIVE_METHOD(ClassName, MethodName)                                                   \
  [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {                               \
    ClassName *self = inst->as<ClassName>();                                                       \
    return self->MethodName(vm, argc, argv);                                                       \
  }

// 定义简单读取器 (getter) 的宏
#define SPT_GETTER(ClassName, MemberName, ToValueFn)                                               \
  [](VM *vm, NativeInstance *inst) -> Value {                                                      \
    ClassName *self = inst->as<ClassName>();                                                       \
    return ToValueFn(vm, self->MemberName);                                                        \
  }

// 定义简单设置器 (setter) 的宏
#define SPT_SETTER(ClassName, MemberName, FromValueFn)                                             \
  [](VM *vm, NativeInstance *inst, Value value) {                                                  \
    ClassName *self = inst->as<ClassName>();                                                       \
    self->MemberName = FromValueFn(value);                                                         \
  }

} // namespace spt

// ============================================================================
// 模板实现 - 必须在头文件中以确保正确的实例化
// ============================================================================

// 放置在类声明之后以避免循环依赖
#include "VM.h"

namespace spt {

// ============================================================================
// ValueConverter<std::string>::to 实现
// ============================================================================

inline Value native_detail::ValueConverter<std::string>::to(VM *vm, const std::string &value) {
  return Value::object(vm->allocateString(value));
}

// ============================================================================
// NativeClassBuilder 实现
// ============================================================================

template <typename T>
NativeClassBuilder<T>::NativeClassBuilder(VM *vm, const std::string &name) : vm_(vm) {
  class_ = vm_->allocateNativeClass(name);
  class_->typeId = getTypeId<T>();
  class_->instanceDataSize = sizeof(T);

  // 设置默认析构函数用于 RAII 清理
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
  // 在全局注册表中注册
  NativeBindingRegistry::instance().registerClass(class_->typeId, class_);

  // 在 VM 中注册为全局变量
  vm_->defineGlobal(class_->name, Value::object(class_));

  return class_;
}

// ============================================================================
// 模板实用函数实现
// ============================================================================

template <typename T> NativeInstance *wrapNativeObject(VM *vm, T *obj, OwnershipMode ownership) {
  NativeClassObject *nativeClass = NativeBindingRegistry::instance().findClass(getTypeId<T>());
  if (!nativeClass) {
    return nullptr; // 类型未注册
  }

  NativeInstance *instance = vm->allocateNativeInstance(nativeClass);
  instance->data = obj;
  instance->ownership = ownership;
  return instance;
}

template <typename T, typename... Args> NativeInstance *createNativeObject(VM *vm, Args &&...args) {
  NativeClassObject *nativeClass = NativeBindingRegistry::instance().findClass(getTypeId<T>());
  if (!nativeClass) {
    return nullptr; // 类型未注册
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
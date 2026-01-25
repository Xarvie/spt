#pragma once

#include "../Common/Types.h"
#include "StringTable.h"
#include "unordered_dense.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace spt {
struct Value;
struct StringObject;
} // namespace spt

namespace std {
// 为 spt::Value 提供 std::hash 特化，以便将其用作标准库容器的键
template <> struct hash<spt::Value> {
  size_t operator()(const spt::Value &v) const noexcept;
};
} // namespace std

namespace spt {

// ============================================================================
// FNV-1a 哈希算法 - 内联定义以便在头文件中使用
// ============================================================================
inline constexpr uint32_t FNV_OFFSET_BASIS = 2166136261u;
inline constexpr uint32_t FNV_PRIME = 16777619u;

inline uint32_t fnv1a_hash(const char *str, size_t len) noexcept {
  uint32_t hash = FNV_OFFSET_BASIS;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint8_t>(str[i]);
    hash *= FNV_PRIME;
  }
  return hash;
}

inline uint32_t fnv1a_hash(std::string_view sv) noexcept {
  return fnv1a_hash(sv.data(), sv.size());
}

// ============================================================================
// GC 对象基类 - 在此处定义以避免循环依赖
// 注意：不使用虚析构函数，GC 通过 type 字段手动分发并执行内存释放
// ============================================================================
struct GCObject {
  GCObject *next = nullptr; // 用于 GC 追踪所有对象的链表
  ValueType type;           // 对象类型标识
  bool marked = false;      // GC 标记位（三色标记法中使用）
};

// ============================================================================
// StringObject - 带有内联字符存储的紧凑内存布局
// ============================================================================
//
// 内存布局:
// +------------------+
// | GCObject 字段    |  (next, type, marked)
// +------------------+
// | hash (uint32_t)  |  预先计算的 FNV-1a 哈希值
// +------------------+
// | length (uint32_t)|  字符串长度
// +------------------+
// | chars[0..length] |  以 null 结尾的字符数据（紧随结构体之后）
// +------------------+
//
// 核心特性:
// - 所有字符串都是驻留的（相同内容 → 相同指针）
// - 哈希值在创建时计算一次，永不重复计算
// - 字符串相等性比较简化为指针比较
// - 整个对象与字符串数据通过单次内存分配完成
// ============================================================================
struct StringObject : GCObject {
  uint32_t hash;   // 预计算的 FNV-1a 哈希
  uint32_t length; // 字符串长度（不计入结束符 \0）

  // === 访问器 ===
  // 字符数据存储在此结构体实例之后的相邻内存中
  const char *chars() const noexcept { return reinterpret_cast<const char *>(this + 1); }

  char *chars() noexcept { return reinterpret_cast<char *>(this + 1); }

  std::string_view view() const noexcept { return std::string_view(chars(), length); }

  // 用于兼容需要 std::string 的代码
  std::string str() const { return std::string(chars(), length); }

  // 获取以 null 结尾的 C 风格字符串
  const char *c_str() const noexcept { return chars(); }

  // === 尺寸计算 ===
  // 计算分配该对象所需的总内存大小
  static size_t allocationSize(size_t strLen) noexcept { return sizeof(StringObject) + strLen + 1; }

  size_t allocationSize() const noexcept { return allocationSize(length); }

  // === 比较 ===
  bool equals(std::string_view sv) const noexcept {
    return length == sv.size() && std::memcmp(chars(), sv.data(), length) == 0;
  }

  bool equals(const StringObject *other) const noexcept {
    // 快速路径：指针相等（相同的驻留字符串）
    if (this == other)
      return true;
    if (!other)
      return false;
    // 快速路径：哈希不匹配
    if (hash != other->hash)
      return false;
    // 全量比较（对于驻留字符串，很少会走到这一步）
    return length == other->length && std::memcmp(chars(), other->chars(), length) == 0;
  }

private:
  StringObject() { type = ValueType::String; }
  friend class StringPool;
  friend class GC;
};

// ============================================================================
// 透明哈希/相等性比较 - 用于 Map 中的 StringObject* 键
// 支持异构查找：无需创建对象即可通过 string_view 进行查找
// ============================================================================

struct StringPtrHash {
  using is_transparent = void;

  size_t operator()(const StringObject *str) const noexcept { return str ? str->hash : 0; }

  size_t operator()(std::string_view sv) const noexcept { return fnv1a_hash(sv); }
};

inline uint32_t IdentityStringHash::hash(const StringObject *s) noexcept { return s ? s->hash : 0; }

struct StringPtrEqual {
  using is_transparent = void;

  bool operator()(const StringObject *a, const StringObject *b) const noexcept {
    // 对于驻留字符串，指针相等性判断就足够了
    return a == b;
  }

  bool operator()(const StringObject *str, std::string_view sv) const noexcept {
    return str && str->equals(sv);
  }

  bool operator()(std::string_view sv, const StringObject *str) const noexcept {
    return str && str->equals(sv);
  }
};

// 便捷别名：以 StringObject* 指针为键的 Map
template <typename V> using StringMap = spt::SptHashTable<V>;

// ============================================================================
// Value - 标签联合体 (Tagged Union)
// 注意：该实现禁止使用 NaN-boxing 技术
// ============================================================================
struct Value {
  ValueType type; // 显式类型标签

  union {
    bool boolean;
    int64_t integer;
    double number;
    GCObject *gc; // 存储堆对象指针：String, List, Map, Object, Closure, Fiber 等
  } as;

  // ========================================================================
  // 静态构造函数
  // ========================================================================
  static Value nil() {
    Value v;
    v.type = ValueType::Nil;
    v.as.gc = nullptr;
    return v;
  }

  static Value boolean(bool b) {
    Value v;
    v.type = ValueType::Bool;
    v.as.boolean = b;
    return v;
  }

  static Value integer(int64_t i) {
    Value v;
    v.type = ValueType::Int;
    v.as.integer = i;
    return v;
  }

  static Value number(double n) {
    Value v;
    v.type = ValueType::Float;
    v.as.number = n;
    return v;
  }

  static Value object(GCObject *obj) {
    Value v;
    v.type = obj ? obj->type : ValueType::Nil;
    v.as.gc = obj;
    return v;
  }

  // ========================================================================
  // 类型检查
  // ========================================================================
  bool isNil() const { return type == ValueType::Nil; }

  bool isBool() const { return type == ValueType::Bool; }

  bool isInt() const { return type == ValueType::Int; }

  bool isFloat() const { return type == ValueType::Float; }

  bool isNumber() const { return type == ValueType::Float || type == ValueType::Int; }

  bool isString() const { return type == ValueType::String; }

  bool isList() const { return type == ValueType::List; }

  bool isMap() const { return type == ValueType::Map; }

  bool isInstance() const { return type == ValueType::Object; }

  bool isClosure() const { return type == ValueType::Closure; }

  bool isClass() const { return type == ValueType::Class; }

  bool isNativeFunc() const { return type == ValueType::NativeFunc; }

  bool isFiber() const { return type == ValueType::Fiber; }

  bool isNativeClass() const { return type == ValueType::NativeClass; }

  bool isNativeInstance() const { return type == ValueType::NativeObject; }

  // 检查是否可调用（闭包、原生函数、类构造函数等）
  bool isCallable() const {
    return type == ValueType::Closure || type == ValueType::NativeFunc ||
           type == ValueType::Class || type == ValueType::NativeClass;
  }

  // 检查是否为任何类型的实例
  bool isAnyInstance() const {
    return type == ValueType::Object || type == ValueType::NativeObject;
  }

  // 检查是否为任何类型的类定义
  bool isAnyClass() const { return type == ValueType::Class || type == ValueType::NativeClass; }

  // ========================================================================
  // 值提取（强制转换/获取）
  // ========================================================================
  bool asBool() const { return as.boolean; }

  int64_t asInt() const { return as.integer; }

  double asFloat() const { return as.number; }

  // 统一获取数值（如果是整数则转换为浮点数）
  double asNumber() const {
    return type == ValueType::Int ? static_cast<double>(as.integer) : as.number;
  }

  GCObject *asGC() const { return as.gc; }

  // 字符串专用提取器
  StringObject *asString() const { return static_cast<StringObject *>(as.gc); }

  // ========================================================================
  // 转换与逻辑判断
  // ========================================================================
  std::string toString() const;
  bool isTruthy() const; // 判断逻辑真假值

  // ========================================================================
  // 元数据信息
  // ========================================================================
  const char *typeName() const;

  // ========================================================================
  // 比较与哈希
  // ========================================================================
  bool equals(const Value &other) const;
  size_t hash() const noexcept;

  friend bool operator==(const Value &lhs, const Value &rhs) { return lhs.equals(rhs); }

  friend bool operator!=(const Value &lhs, const Value &rhs) { return !(lhs == rhs); }
};

// ============================================================================
// 列表对象 (ListObject)
// ============================================================================
struct ListObject : GCObject {
  std::vector<Value> elements;

  ListObject() { type = ValueType::List; }
};

// ============================================================================
// Map 对象 (MapObject)
// ============================================================================
struct MapObject : GCObject {
  // 使用 spt::Value 作为键值对的哈希表
  ankerl::unordered_dense::map<Value, Value> entries;
  template <typename V> using StringMap = spt::SptHashTable<V>;

  MapObject() { type = ValueType::Map; }

  Value get(const Value &key) const;
  void set(const Value &key, const Value &value);
  bool has(const Value &key) const;
};

// ============================================================================
// Value::isTruthy 内联实现
// 决定哪些值在 if/while 语句中被视为 "真"
// ============================================================================
inline bool Value::isTruthy() const {
  if (type == ValueType::Nil)
    return false;
  if (type == ValueType::Bool)
    return as.boolean;
  if (type == ValueType::Int)
    return as.integer != 0;
  if (type == ValueType::Float)
    return as.number != 0.0;
  // 所有其他引用类型（对象、闭包等）均为真
  return true;
}

} // namespace spt

// std::hash 委托给 Value 结构体内部的 hash() 实现
inline size_t std::hash<spt::Value>::operator()(const spt::Value &v) const noexcept {
  return v.hash();
}
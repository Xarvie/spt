#pragma once

#include "../Common/Types.h"
#include "unordered_dense.h"
#include <cstdint>
#include <string>
#include <vector>

namespace spt {
struct Value;
}

namespace std {
template <> struct hash<spt::Value> {
  size_t operator()(const spt::Value &v) const noexcept;
};
} // namespace std

namespace spt {

// GC 对象基类 - 在这里定义以避免循环依赖
// 注意：不使用虚析构函数，GC通过type字段手动分发释放
struct GCObject {
  GCObject *next = nullptr; // GC 链表
  ValueType type;           // 对象类型
  bool marked = false;      // GC 标记
};

// tagged union （禁止使用NaN-boxing）
struct Value {
  ValueType type;

  union {
    bool boolean;
    int64_t integer;
    double number;
    GCObject *gc; // String, List, Map, Object, Closure, Fiber, NativeClass, NativeObject 等
  } as;

  // ========================================================================
  // 构造函数
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

  // 纯浮点数（不包括整数）
  bool isFloat() const { return type == ValueType::Float; }

  // 数字（整数或浮点数）
  bool isNumber() const { return type == ValueType::Float || type == ValueType::Int; }

  bool isString() const { return type == ValueType::String; }

  bool isList() const { return type == ValueType::List; }

  bool isMap() const { return type == ValueType::Map; }

  // 实例对象 (script-defined class instance)
  bool isInstance() const { return type == ValueType::Object; }

  bool isClosure() const { return type == ValueType::Closure; }

  bool isClass() const { return type == ValueType::Class; }

  // 原生函数
  bool isNativeFunc() const { return type == ValueType::NativeFunc; }

  // Fiber 检查
  bool isFiber() const { return type == ValueType::Fiber; }

  // === 原生绑定类型检查 ===

  // 是否为原生类（已在 VM 中注册的 C++ 类）
  bool isNativeClass() const { return type == ValueType::NativeClass; }

  // 是否为原生实例（包装在 NativeInstance 中的 C++ 对象）
  bool isNativeInstance() const { return type == ValueType::NativeObject; }

  // 是否为任何可调用对象：闭包、原生函数或原生类（构造函数）
  bool isCallable() const {
    return type == ValueType::Closure || type == ValueType::NativeFunc ||
           type == ValueType::Class || type == ValueType::NativeClass;
  }

  // 是否为任何对象实例：脚本定义的实例或原生 C++ 实例
  bool isAnyInstance() const {
    return type == ValueType::Object || type == ValueType::NativeObject;
  }

  // 是否为任何类类型：脚本定义的类或原生 C++ 类
  bool isAnyClass() const { return type == ValueType::Class || type == ValueType::NativeClass; }

  // ========================================================================
  // 值提取
  // ========================================================================
  bool asBool() const { return as.boolean; }

  int64_t asInt() const { return as.integer; }

  // 获取浮点数（只能用于 Float 类型）
  double asFloat() const { return as.number; }

  // 获取数字（自动转换 Int -> Float）
  double asNumber() const {
    return type == ValueType::Int ? static_cast<double>(as.integer) : as.number;
  }

  GCObject *asGC() const { return as.gc; }

  // ========================================================================
  // 转换
  // ========================================================================
  std::string toString() const;

  // 真值性（python 风格： nil , false , 0 , 0.0 和空字符串为假）
  bool isTruthy() const;

  // ========================================================================
  // 类型信息
  // ========================================================================
  const char *typeName() const; // 返回类型名称字符串

  // ========================================================================
  // 比较
  // ========================================================================
  bool equals(const Value &other) const;

  size_t hash() const noexcept;

  friend bool operator==(const Value &lhs, const Value &rhs) { return lhs.equals(rhs); }

  friend bool operator!=(const Value &lhs, const Value &rhs) { return !(lhs == rhs); }
};

// ============================================================================
// 字符串对象
// ============================================================================
struct StringObject : GCObject {
  std::string data;
  uint32_t hash;

  StringObject() { type = ValueType::String; }
};

// ============================================================================
// 列表对象
// ============================================================================
struct ListObject : GCObject {
  std::vector<Value> elements;

  ListObject() { type = ValueType::List; }
};

// ============================================================================
// Map 对象
// ============================================================================
struct MapObject : GCObject {
  ankerl::unordered_dense::map<Value, Value> entries;

  MapObject() { type = ValueType::Map; }

  Value get(const Value &key) const;
  void set(const Value &key, const Value &value);
  bool has(const Value &key) const;
};

// ============================================================================
// Value::isTruthy 内联实现
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
  return true;
}

} // namespace spt

inline size_t std::hash<spt::Value>::operator()(const spt::Value &v) const noexcept {
  return v.hash();
}
#pragma once

#include "../Common/Types.h"
#include <cstdint>
#include <string>
#include <vector>

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
    GCObject *gc; // String, List, Map, Object, Closure 等
  } as;

  // ========================================================================
  // 构造函数
  // ========================================================================
  static Value nil();
  static Value boolean(bool b);
  static Value integer(int64_t i);
  static Value number(double n);
  static Value object(GCObject *obj);

  // ========================================================================
  // 类型检查
  // ========================================================================
  bool isNil() const;
  bool isBool() const;
  bool isInt() const;
  bool isFloat() const;  // 纯浮点数（不包括整数）
  bool isNumber() const; // 数字（整数或浮点数）
  bool isString() const;
  bool isList() const;
  bool isMap() const;
  bool isObject() const;   // 已废弃，使用 isInstance()
  bool isInstance() const; // 实例对象
  bool isClosure() const;
  bool isClass() const;
  bool isNativeFunc() const; // 原生函数

  // ========================================================================
  // 值提取
  // ========================================================================
  bool asBool() const;
  int64_t asInt() const;
  double asFloat() const;  // 获取浮点数（只能用于 Float 类型）
  double asNumber() const; // 获取数字（自动转换 Int -> Float）
  GCObject *asGC() const;

  // ========================================================================
  // 转换
  // ========================================================================
  std::string toString() const;
  bool toBool() const;
  bool isTruthy() const; // 真值性（Lua 风格：只有 nil 和 false 为假）

  // ========================================================================
  // 类型信息
  // ========================================================================
  const char *typeName() const; // 返回类型名称字符串

  // ========================================================================
  // 比较
  // ========================================================================
  bool equals(const Value &other) const;
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
  // 简化实现，生产环境应使用更高效的哈希表
  std::vector<std::pair<Value, Value>> entries;

  MapObject() { type = ValueType::Map; }

  Value get(const Value &key) const;
  void set(const Value &key, const Value &value);
  bool has(const Value &key) const;
};

} // namespace spt
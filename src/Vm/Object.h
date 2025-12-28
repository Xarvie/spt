#pragma once

#include "../Common/Types.h"
#include "Value.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace spt {

// ============================================================================
// UpValue - 闭包捕获的变量
// ============================================================================
struct UpValue : GCObject {
  Value *location;             // 指向栈上的值或 closed 值
  Value closed;                // 关闭后的值
  UpValue *nextOpen = nullptr; // 开放 UpValue 链表

  UpValue() { type = ValueType::Upvalue; }
};

// ============================================================================
// 闭包对象
// ============================================================================
struct Closure : GCObject {
  const Prototype *proto; // 函数原型
  std::vector<UpValue *> upvalues;

  Closure() { type = ValueType::Closure; }
};

// ============================================================================
// 类对象
// ============================================================================
struct ClassObject : GCObject {
  std::string name;
  std::unordered_map<std::string, Value> methods; // 方法表（热更新友好）
  std::unordered_map<std::string, Value> statics; // 静态成员

  ClassObject() { type = ValueType::Class; }
};

// ============================================================================
// 实例对象
// ============================================================================
struct Instance : GCObject {
  ClassObject *klass;
  std::unordered_map<std::string, Value> fields; // 实例字段

  Instance() { type = ValueType::Object; }

  // 便利方法：字段访问（兼容 MapObject 风格的 API）
  Value getField(const std::string &name) const {
    auto it = fields.find(name);
    return (it != fields.end()) ? it->second : Value::nil();
  }

  void setField(const std::string &name, const Value &value) { fields[name] = value; }

  bool hasField(const std::string &name) const { return fields.find(name) != fields.end(); }
};

// ============================================================================
// 原生函数
// ============================================================================
using NativeFn = std::function<Value(int argc, Value *args)>;

struct NativeFunction : public GCObject {
  std::string name;
  NativeFn function;
  int arity;
  uint8_t flags = FUNC_NONE;

  NativeFunction() { type = ValueType::NativeFunc; }
};

} // namespace spt
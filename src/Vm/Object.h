#pragma once

#include "../Common/Types.h"
#include "Value.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace spt {

class VM;

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
  Value gcMethod;                                 // 缓存的 __gc 终结器方法

  ClassObject() : gcMethod(Value::nil()) { type = ValueType::Class; }

  // 检查是否有终结器
  bool hasFinalizer() const { return !gcMethod.isNil(); }

  // 设置方法（自动检测 __gc）
  void setMethod(const std::string &name, Value method) {
    methods[name] = method;
    if (name == "__gc") {
      gcMethod = method;
    }
  }
};

// ============================================================================
// 实例对象
// ============================================================================
struct Instance : GCObject {
  ClassObject *klass;
  std::unordered_map<std::string, Value> fields; // 实例字段
  bool isFinalized = false;                      // 是否已执行 __gc 终结器

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
using NativeFn = std::function<Value(VM *vm, Value receiver, int argc, Value *args)>;

struct NativeFunction : public GCObject {
  std::string name;
  NativeFn function;
  int arity;
  // 对于全局函数(print)，它是 nil。
  // 对于方法(list.push)，它是那个 list 对象。
  Value receiver = Value::nil();

  NativeFunction() {
    type = ValueType::NativeFunc;
    receiver = Value::nil();
  }
};

struct MethodEntry {
  const char *name;
  NativeFn fn;
  int arity;
};

} // namespace spt
#pragma once

#include "../Common/Types.h"
#include "Value.h"
#include "unordered_dense.h"
#include <functional>
#include <string>
#include <vector>

namespace spt {

class VM;

// ============================================================================
// UpValue - 闭包捕获的变量
// ============================================================================
// 当函数访问其外部作用域的变量时，该变量会被捕获为 UpValue。
// 它可以处于“开放”状态（指向栈）或“关闭”状态（拷贝到堆中）。
struct UpValue : GCObject {
  Value *location;             // 指向栈上的值或 closed 成员变量
  Value closed;                // 当变量超出作用域被“关闭”后，值存储在这里
  UpValue *nextOpen = nullptr; // 用于追踪所有开放状态 UpValue 的链表指针

  UpValue() { type = ValueType::Upvalue; }
};

// ============================================================================
// 闭包对象 (Closure)
// ============================================================================
// 函数在运行时的表现形式，包含了函数原型以及捕获的 UpValue 列表。
struct Closure : GCObject {
  const Prototype *proto; // 函数原型（字节码、常量等）
  int upvalueCount;       // 捕获的 UpValue 数量
  UpValue *upvalues[];    // 柔性数组，存储指向 UpValue 的指针

  Closure() = delete; // 闭包需要动态分配内存以容纳 upvalues
};

// ============================================================================
// 类对象 - 使用 StringObject* 作为方法表键
// ============================================================================
struct ClassObject : GCObject {
  std::string name; // 类名

  // 方法表：StringObject* -> Value (闭包或原生函数)
  // 使用 StringObject* 指针作为键可以实现 O(1) 的快速查找
  StringMap<Value> methods;

  // 静态成员表
  StringMap<Value> statics;

  // 缓存的 __gc 终结器方法，用于快速查找
  Value gcMethod;

  ClassObject() : gcMethod(Value::nil()) { type = ValueType::Class; }

  // 检查类是否定义了终结器
  bool hasFinalizer() const { return !gcMethod.isNil(); }

  // 设置方法（会自动检测并缓存 __gc 方法）
  void setMethod(StringObject *name, Value method) {
    methods[name] = method;
    // 检查是否为 __gc 方法
    if (name->equals("__gc")) {
      gcMethod = method;
    }
  }
};

// ============================================================================
// 实例对象 - 使用 StringObject* 作为字段表键
// ============================================================================
struct Instance : GCObject {
  ClassObject *klass; // 所属的类

  // 实例字段表：StringObject* -> Value
  StringMap<Value> fields;

  bool isFinalized = false; // 标识是否已经执行过 __gc 终结器

  Instance() { type = ValueType::Object; }

  // === 推荐使用 StringObject* 键进行字段访问 ===

  Value getField(StringObject *name) const {
    if (const Value *v = fields.get(name)) {
      return *v;
    }
    return Value::nil();
  }

  void setField(StringObject *name, const Value &value) { fields[name] = value; }

  bool hasField(StringObject *name) const { return fields.find(name) != fields.end(); }
};

// ============================================================================
// 原生实例 - 包装 C++ 对象指针
// ============================================================================
struct NativeInstance : GCObject {
  ClassObject *klass;       // 所属的类（__gc 在这里）
  void *data = nullptr;     // C++ 对象指针
  bool isFinalized = false; // 标识是否已执行过 __gc
  StringMap<Value> fields;  // 脚本层字段

  NativeInstance() { type = ValueType::NativeObject; }

  bool isValid() const { return data != nullptr; }

  Value getField(StringObject *name) const {
    if (const Value *v = fields.get(name)) {
      return *v;
    }
    return Value::nil();
  }

  void setField(StringObject *name, const Value &value) { fields[name] = value; }

  bool hasField(StringObject *name) const { return fields.find(name) != fields.end(); }
};

// ============================================================================
// 原生函数 (Native Function)
// ============================================================================
// C++ 实现的函数，可被 VM 调用
using NativeFn = std::function<Value(VM *vm, Value receiver, int argc, Value *args)>;

struct NativeFunction : public GCObject {
  std::string name;              // 函数名（用于调试）
  NativeFn function;             // C++ 函数包装器
  int arity;                     // 参数个数（参数秩）
  Value receiver = Value::nil(); // 接收者（对于绑定方法）

  NativeFunction() {
    type = ValueType::NativeFunc;
    receiver = Value::nil();
  }
};

// ============================================================================
// 方法条目 - 用于 C 模块注册
// ============================================================================
struct MethodEntry {
  const char *name; // 方法名
  NativeFn fn;      // 函数实现
  int arity;        // 参数个数
};

} // namespace spt
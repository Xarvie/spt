#pragma once

#include "../Common/Types.h"
#include "Value.h"
#include "unordered_dense.h"
#include <string>
#include <vector>

namespace spt {

class VM;
struct Closure;

// ============================================================================
// UpValue - 闭包捕获的变量
// ============================================================================
// 当函数访问其外部作用域的变量时，该变量会被捕获为 UpValue。
// 它可以处于"开放"状态（指向栈）或"关闭"状态（拷贝到堆中）。
struct UpValue : GCObject {
  Value *location;             // 指向栈上的值或 closed 成员变量
  Value closed;                // 当变量超出作用域被"关闭"后，值存储在这里
  UpValue *nextOpen = nullptr; // 用于追踪所有开放状态 UpValue 的链表指针

  UpValue() { type = ValueType::Upvalue; }
};

// ============================================================================
// 闭包类型 (ClosureKind)
// ============================================================================
enum class ClosureKind : uint8_t {
  Script, // 脚本闭包（字节码函数）
  Native, // 原生闭包（C++ 函数）
};

// ============================================================================
// 原生函数签名 (NativeFn)
// ============================================================================
// C++ 实现的函数，可被 VM 调用
//
// 返回值含义 (int):
//   - 正数 N: 已将 N 个返回值压入栈顶
//   - 0: 无返回值
//
// 注意: 使用普通函数指针而非 std::function，以支持 setjmp/longjmp 错误处理

using NativeFn = int (*)(VM *vm, Closure *self, int argc, Value *args);

// ============================================================================
// 闭包对象 (Closure) - 统一脚本闭包和原生闭包
// ============================================================================
// 函数在运行时的表现形式，可以是脚本函数或原生 C++ 函数。
//
// 内存布局（柔性数组）:
// - Script 闭包: upvalues[] 存储 UpValue* 指针
// - Native 闭包: upvalues[] 存储 Value（直接存储捕获的值）
//
// +------------------+
// | GCObject 字段    |
// +------------------+
// | kind             |  Script 或 Native
// +------------------+
// | upvalueCount     |  捕获的 upvalue 数量
// +------------------+
// | proto / function |  脚本原型 或 原生函数指针
// +------------------+
// | name             |  函数名（原生函数用）
// +------------------+
// | arity            |  参数个数（原生函数用，-1 表示变参）
// +------------------+
// | receiver         |  接收者（绑定方法用）
// +------------------+
// | upvalues[]       |  柔性数组
// +------------------+
struct Closure : GCObject {
  ClosureKind kind;     // 闭包类型
  uint8_t upvalueCount; // upvalue 数量

  // === 脚本闭包专用字段 ===
  const Prototype *proto = nullptr; // 函数原型（字节码、常量等）

  // === 原生闭包专用字段 ===
  NativeFn function;            // C++ 函数包装器（仅 Native 有效）
  StringObject *name = nullptr; // 函数名（用于调试）
  int arity = 0;                // 参数个数（-1 表示变参）
  Value receiver;               // 接收者（绑定方法用）

  // === 柔性数组 ===
  // Script 闭包: UpValue* upvalues[]
  // Native 闭包: Value upvalues[]
  // 由于两者大小不同，使用 union 或分别访问
  union {
    UpValue *scriptUpvalues[0]; // Script 闭包的 upvalue 指针数组
    Value nativeUpvalues[0];    // Native 闭包的 upvalue 值数组
  };

  // 禁用默认构造
  Closure() = delete;

  // === 类型检查 ===
  bool isScript() const { return kind == ClosureKind::Script; }

  bool isNative() const { return kind == ClosureKind::Native; }

  // === Script 闭包的 UpValue 访问 ===
  UpValue *getScriptUpvalue(int index) const {
    return (isScript() && index >= 0 && index < upvalueCount) ? scriptUpvalues[index] : nullptr;
  }

  void setScriptUpvalue(int index, UpValue *uv) {
    if (isScript() && index >= 0 && index < upvalueCount) {
      scriptUpvalues[index] = uv;
    }
  }

  // === Native 闭包的 UpValue 访问 ===
  Value getNativeUpvalue(int index) const {
    return (isNative() && index >= 0 && index < upvalueCount) ? nativeUpvalues[index]
                                                              : Value::nil();
  }

  void setNativeUpvalue(int index, Value val) {
    if (isNative() && index >= 0 && index < upvalueCount) {
      nativeUpvalues[index] = val;
    }
  }

  // === 兼容性访问器（用于从旧 NativeFunction 迁移的代码） ===
  Value getUpvalue(int index) const { return getNativeUpvalue(index); }

  void setUpvalue(int index, Value val) { setNativeUpvalue(index, val); }

  // === 获取名称字符串（安全访问） ===
  const char *getName() const {
    if (isNative()) {
      return name ? name->c_str() : "<anonymous>";
    } else {
      return proto ? proto->name.c_str() : "<script>";
    }
  }

  std::string_view getNameView() const {
    if (isNative()) {
      return name ? name->view() : std::string_view("<anonymous>");
    } else {
      return proto ? std::string_view(proto->name) : std::string_view("<script>");
    }
  }

  // === 内存布局计算 ===
  static constexpr size_t baseSize() { return sizeof(Closure); }

  // Script 闭包的分配大小
  static size_t scriptAllocSize(int nupvalues) {
    return baseSize() + static_cast<size_t>(nupvalues) * sizeof(UpValue *);
  }

  // Native 闭包的分配大小
  static size_t nativeAllocSize(int nupvalues) {
    return baseSize() + static_cast<size_t>(nupvalues) * sizeof(Value);
  }

  // 获取当前闭包的分配大小
  size_t allocSize() const {
    if (isScript()) {
      return scriptAllocSize(upvalueCount);
    } else {
      return nativeAllocSize(upvalueCount);
    }
  }
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
// 方法条目 - 用于 C 模块注册
// ============================================================================
struct MethodEntry {
  const char *name; // 方法名
  NativeFn fn;      // 函数实现
  int arity;        // 参数个数
};

} // namespace spt
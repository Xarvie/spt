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
// MagicMethod - 魔术方法索引枚举
// ============================================================================
// 定义了所有支持的魔术方法，用于 ClassObject::magicMethods[] 数组索引
// 注意：MM_MAX 必须保持在最后，用于确定数组大小
enum MagicMethod : uint8_t {
  // === 生命周期 ===
  MM_INIT, // __init (构造函数)
  MM_GC,   // __gc (终结器/析构函数)

  // === 属性访问 ===
  MM_GET,       // __get (属性获取回退，当字段不存在时调用)
  MM_SET,       // __set (属性设置拦截)
  MM_INDEX_GET, // __getitem (下标读取 obj[key])
  MM_INDEX_SET, // __setitem (下标写入 obj[key] = value)

  // === 算术运算 ===
  MM_ADD,  // __add (+)
  MM_SUB,  // __sub (-)
  MM_MUL,  // __mul (*)
  MM_DIV,  // __div (/)
  MM_MOD,  // __mod (%)
  MM_POW,  // __pow (**)
  MM_UNM,  // __unm (一元负号 -x)
  MM_IDIV, // __idiv (整除 ~/)

  // === 比较运算 ===
  MM_EQ, // __eq (==)
  MM_LT, // __lt (<)
  MM_LE, // __le (<=)

  // === 位运算 ===
  MM_BAND, // __band (&)
  MM_BOR,  // __bor (|)
  MM_BXOR, // __bxor (^)
  MM_BNOT, // __bnot (~)
  MM_SHL,  // __shl (<<)
  MM_SHR,  // __shr (>>)

  MM_MAX // 魔术方法总数（必须在最后）
};

// ============================================================================
// ClassFlag - 类特性标志位
// ============================================================================
// 每个 MagicMethod 都有一个对应的标志位，用于 O(1) 检测类是否定义了某魔术方法
// 使用位标记避免频繁的哈希表查找
enum ClassFlag : uint32_t {
  CLASS_NONE = 0,

  // === 生命周期 ===
  CLASS_HAS_INIT = 1u << MM_INIT,
  CLASS_HAS_GC = 1u << MM_GC,

  // === 属性访问 ===
  CLASS_HAS_GET = 1u << MM_GET,
  CLASS_HAS_SET = 1u << MM_SET,
  CLASS_HAS_INDEX_GET = 1u << MM_INDEX_GET,
  CLASS_HAS_INDEX_SET = 1u << MM_INDEX_SET,

  // === 算术运算 ===
  CLASS_HAS_ADD = 1u << MM_ADD,
  CLASS_HAS_SUB = 1u << MM_SUB,
  CLASS_HAS_MUL = 1u << MM_MUL,
  CLASS_HAS_DIV = 1u << MM_DIV,
  CLASS_HAS_MOD = 1u << MM_MOD,
  CLASS_HAS_POW = 1u << MM_POW,
  CLASS_HAS_UNM = 1u << MM_UNM,
  CLASS_HAS_IDIV = 1u << MM_IDIV,

  // === 比较运算 ===
  CLASS_HAS_EQ = 1u << MM_EQ,
  CLASS_HAS_LT = 1u << MM_LT,
  CLASS_HAS_LE = 1u << MM_LE,

  // === 位运算 ===
  CLASS_HAS_BAND = 1u << MM_BAND,
  CLASS_HAS_BOR = 1u << MM_BOR,
  CLASS_HAS_BXOR = 1u << MM_BXOR,
  CLASS_HAS_BNOT = 1u << MM_BNOT,
  CLASS_HAS_SHL = 1u << MM_SHL,
  CLASS_HAS_SHR = 1u << MM_SHR,

  // === 复合标志（用于快速检测） ===
  CLASS_HAS_ANY_ARITHMETIC = CLASS_HAS_ADD | CLASS_HAS_SUB | CLASS_HAS_MUL | CLASS_HAS_DIV |
                             CLASS_HAS_MOD | CLASS_HAS_POW | CLASS_HAS_UNM | CLASS_HAS_IDIV,

  CLASS_HAS_ANY_COMPARISON = CLASS_HAS_EQ | CLASS_HAS_LT | CLASS_HAS_LE,

  CLASS_HAS_ANY_BITWISE = CLASS_HAS_BAND | CLASS_HAS_BOR | CLASS_HAS_BXOR | CLASS_HAS_BNOT |
                          CLASS_HAS_SHL | CLASS_HAS_SHR,

  CLASS_HAS_ANY_INDEX = CLASS_HAS_INDEX_GET | CLASS_HAS_INDEX_SET,

  CLASS_HAS_ANY_PROPERTY = CLASS_HAS_GET | CLASS_HAS_SET,
};

// 获取 MagicMethod 对应的标志位
inline constexpr uint32_t getMagicMethodFlag(MagicMethod mm) {
  return (mm < MM_MAX) ? (1u << mm) : 0u;
}

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

static constexpr struct MagicMethodEntry {
  const char *name;  // 魔术方法名（以 __ 开头）
  MagicMethod index; // 对应的枚举值
} kMagicMethodTable[] = {
    // === 生命周期 ===
    {"__init", MM_INIT},
    {"__gc", MM_GC},

    // === 属性访问 ===
    {"__get", MM_GET},
    {"__set", MM_SET},
    {"__getitem", MM_INDEX_GET},
    {"__setitem", MM_INDEX_SET},

    // === 算术运算 ===
    {"__add", MM_ADD},
    {"__sub", MM_SUB},
    {"__mul", MM_MUL},
    {"__div", MM_DIV},
    {"__mod", MM_MOD},
    {"__pow", MM_POW},
    {"__unm", MM_UNM},
    {"__idiv", MM_IDIV},

    // === 比较运算 ===
    {"__eq", MM_EQ},
    {"__lt", MM_LT},
    {"__le", MM_LE},

    // === 位运算 ===
    {"__band", MM_BAND},
    {"__bor", MM_BOR},
    {"__bxor", MM_BXOR},
    {"__bnot", MM_BNOT},
    {"__shl", MM_SHL},
    {"__shr", MM_SHR}};

static constexpr size_t kMagicMethodTableSize =
    sizeof(kMagicMethodTable) / sizeof(kMagicMethodTable[0]);

// 编译时断言：确保表大小与枚举值匹配
static_assert(kMagicMethodTableSize == MM_MAX, "MagicMethodTable size must match MM_MAX");

// ============================================================================
// getMagicMethodIndex - 字符串视图版本
// ============================================================================
// 使用优化的查找策略：
// 1. 快速路径：检查是否以 "__" 开头
// 2. 线性扫描（对于小数组比哈希更快）
inline MagicMethod getMagicMethodIndex(std::string_view name) {
  // 快速路径：魔术方法必须以 "__" 开头
  if (name.size() < 3 || name[0] != '_' || name[1] != '_') {
    return MM_MAX; // 不是魔术方法
  }

  // 线性扫描查找
  // 注意：由于表很小（~30项）且访问模式不规则，
  // 线性扫描通常比哈希表更快（更好的缓存性能）
  for (size_t i = 0; i < kMagicMethodTableSize; ++i) {
    if (name == kMagicMethodTable[i].name) {
      return kMagicMethodTable[i].index;
    }
  }

  return MM_MAX; // 未知的魔术方法名
}

// ============================================================================
// getMagicMethodIndex - StringObject* 版本
// ============================================================================
inline MagicMethod getMagicMethodIndex(const StringObject *nameStr) {
  if (!nameStr) {
    return MM_MAX;
  }
  return getMagicMethodIndex(nameStr->view());
}

// ============================================================================
// 类对象 (ClassObject) - 带魔术方法 VTable 的高性能类元数据
// ============================================================================
// 内存布局优化说明：
// - flags 使用位标记实现 O(1) 魔术方法存在性检测
// - magicMethods[] 数组通过 MagicMethod 枚举直接索引，无需哈希查找
// - 常规方法仍使用 StringMap，保持兼容性和灵活性
struct ClassObject : GCObject {
  std::string name; // 类名

  // === 魔术方法优化字段 ===
  uint32_t flags = CLASS_NONE; // 魔术方法标志位（快速检测）
  Value magicMethods[MM_MAX];  // 魔术方法 VTable（直接索引访问）

  // === 常规方法表 ===
  // 方法表：StringObject* -> Value (闭包或原生函数)
  // 使用 StringObject* 指针作为键可以实现 O(1) 的快速查找
  StringMap<Value> methods;

  // 静态成员表
  StringMap<Value> statics;

  ClassObject() {
    type = ValueType::Class;
    // 初始化所有魔术方法槽位为 nil
    for (int i = 0; i < MM_MAX; ++i) {
      magicMethods[i] = Value::nil();
    }
  }

  // === 魔术方法快速检测（内联，零开销） ===
  bool hasFlag(ClassFlag flag) const { return (flags & flag) != 0; }

  bool hasFlag(uint32_t flag) const { return (flags & flag) != 0; }

  // 检查是否定义了任意算术运算符
  bool hasAnyArithmetic() const { return hasFlag(CLASS_HAS_ANY_ARITHMETIC); }

  // 检查是否定义了任意比较运算符
  bool hasAnyComparison() const { return hasFlag(CLASS_HAS_ANY_COMPARISON); }

  // 检查是否定义了任意位运算符
  bool hasAnyBitwise() const { return hasFlag(CLASS_HAS_ANY_BITWISE); }

  // 检查是否定义了下标访问
  bool hasAnyIndex() const { return hasFlag(CLASS_HAS_ANY_INDEX); }

  // 检查是否定义了属性拦截
  bool hasAnyProperty() const { return hasFlag(CLASS_HAS_ANY_PROPERTY); }

  // === 魔术方法直接访问 ===
  Value getMagicMethod(MagicMethod mm) const {
    return (mm < MM_MAX) ? magicMethods[mm] : Value::nil();
  }

  // 检查类是否定义了终结器（兼容旧 API）
  bool hasFinalizer() const { return hasFlag(CLASS_HAS_GC); }

  // 获取 __gc 方法（兼容旧 API）
  Value getGcMethod() const { return magicMethods[MM_GC]; }

  // === 方法设置（自动检测并缓存魔术方法） ===
  // 设置方法时会自动识别魔术方法名并更新 VTable 和 flags
  void setMethod(StringObject *methodName, Value method) {
    if (!methodName) {
      return;
    }

    // 始终存储到常规方法表（保持完整性）
    methods[methodName] = method;

    // 检测是否为魔术方法
    MagicMethod mm = getMagicMethodIndex(methodName);

    if (mm < MM_MAX) {
      // 更新魔术方法 VTable
      magicMethods[mm] = method;

      // 更新标志位
      if (!method.isNil()) {
        flags |= getMagicMethodFlag(mm);
      } else {
        flags &= ~getMagicMethodFlag(mm);
      }
    }
  }

  // === 辅助方法 ===
  // 直接设置魔术方法（绕过名称检测，用于性能敏感的批量注册）
  void setMagicMethodDirect(MagicMethod mm, Value method) {
    if (mm < MM_MAX) {
      magicMethods[mm] = method;
      if (!method.isNil()) {
        flags |= getMagicMethodFlag(mm);
      } else {
        flags &= ~getMagicMethodFlag(mm);
      }
    }
  }

  // 清除指定魔术方法
  void clearMagicMethod(MagicMethod mm) {
    if (mm < MM_MAX) {
      magicMethods[mm] = Value::nil();
      flags &= ~getMagicMethodFlag(mm);
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

  // === 魔术方法快捷访问 ===
  bool hasMagicMethod(MagicMethod mm) const {
    return klass && klass->hasFlag(getMagicMethodFlag(mm));
  }

  Value getMagicMethod(MagicMethod mm) const {
    return klass ? klass->getMagicMethod(mm) : Value::nil();
  }
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

  // === 魔术方法快捷访问 ===
  bool hasMagicMethod(MagicMethod mm) const {
    return klass && klass->hasFlag(getMagicMethodFlag(mm));
  }

  Value getMagicMethod(MagicMethod mm) const {
    return klass ? klass->getMagicMethod(mm) : Value::nil();
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

// ============================================================================
// 内联辅助函数 - 从 Value 获取类对象（如果有）
// ============================================================================
inline ClassObject *getValueClass(const Value &v) {
  if (v.isInstance()) {
    return static_cast<Instance *>(v.asGC())->klass;
  }
  if (v.isNativeInstance()) {
    return static_cast<NativeInstance *>(v.asGC())->klass;
  }
  return nullptr;
}

// 检查 Value 是否有指定的魔术方法
inline bool valueHasMagicMethod(const Value &v, MagicMethod mm) {
  ClassObject *klass = getValueClass(v);
  return klass && klass->hasFlag(getMagicMethodFlag(mm));
}

// 获取 Value 的魔术方法
inline Value valueGetMagicMethod(const Value &v, MagicMethod mm) {
  ClassObject *klass = getValueClass(v);
  return klass ? klass->getMagicMethod(mm) : Value::nil();
}

} // namespace spt
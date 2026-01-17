#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace spt {

struct Value;

// 值类型标签
enum class ValueType : uint8_t {
  Nil,
  Bool,
  Int,
  Float,
  String,
  List,
  Map,
  Object,
  Closure,
  NativeFunc,
  Class,
  Upvalue,
  Fiber,
  NativeClass,
  NativeObject
};

// 常量值 - 编译期使用
using ConstantValue = std::variant<std::nullptr_t, bool, int64_t, double, std::string>;

// 编译后的指令
using Instruction = uint32_t;

// 函数元数据标志位
enum FunctionFlag : uint8_t {
  FUNC_NONE = 0,       // 普通方法 (默认)：需要隐式 this
  FUNC_VARARG = 1 << 1 // 可变参数
};

class ILineGetter {
public:
  virtual ~ILineGetter() = default;

  virtual int getLine() = 0;
};

struct AbsLineInfo {
  int pc;
  int line;
};

constexpr size_t MaxAbsLine = 128;
constexpr size_t LimitLineDiff = 128;
constexpr uint8_t UseAbsLine = -1;

// ============================================================================
// Prototype - 函数/闭包的编译产物
// ============================================================================
// 包含字节码、常量表、调试信息等。新增 k 数组用于运行时快速常量访问。
struct Prototype {
  std::string name;           // 函数名 (调试用)
  std::string source;         // 源文件名
  std::string short_src;      // 短源文件名
  int lineDefined = 0;        // 起始行号
  int lastLineDefined = 0;    // 结束行号
  uint8_t numParams = 0;      // 参数数量
  uint8_t numUpvalues = 0;    // UpValue 数量
  uint8_t maxStackSize = 0;   // 最大栈深度
  bool isVararg = false;      // 是否可变参数
  bool needsReceiver = false; // 是否需要"this"
  bool useDefer = false;      // 使用defer

  std::vector<Instruction> code;        // 指令序列
  std::vector<ConstantValue> constants; // 常量表 (用于序列化/编译)
  std::vector<AbsLineInfo> absLineInfo; // 绝对行信息 (调试用)
  std::vector<uint8_t> lineInfo;        // 差分行号信息 (调试用)
  std::vector<Prototype> protos;        // 子函数原型
  uint8_t flags = FunctionFlag::FUNC_NONE;

  // UpValue 描述 - 用于闭包创建
  struct UpvalueDesc {
    uint8_t index; // 在父函数中的索引
    bool isLocal;  // true=父函数局部变量, false=父函数UpValue
  };

  std::vector<UpvalueDesc> upvalues;

  // =========================================================================
  // 预编译常量表 - 运行时使用
  // =========================================================================
  // k 数组在 VM 加载时由 preparePrototype() 初始化，直接存储 Value 对象，
  // 避免在热路径上使用 std::visit 访问 ConstantValue variant。
  //
  // 生命周期：
  // - 编译时：k = nullptr, kPrepared = false
  // - 加载时：VM::preparePrototype() 分配并填充 k 数组
  // - 销毁时：析构函数释放 k 数组
  Value *k = nullptr;     // 预编译的 Value 数组
  size_t kCount = 0;      // k 数组大小
  bool kPrepared = false; // 是否已完成预编译

  // =========================================================================
  // 构造与析构
  // =========================================================================
  Prototype() = default;

  // 析构函数：释放 k 数组
  ~Prototype();

  // 移动构造函数
  Prototype(Prototype &&other) noexcept;

  // 移动赋值运算符
  Prototype &operator=(Prototype &&other) noexcept;

  // 禁用拷贝（因为有原始指针成员）
  // 如果需要拷贝，应使用 deepCopy() 方法
  Prototype(const Prototype &) = delete;
  Prototype &operator=(const Prototype &) = delete;

  // 深拷贝方法（如需要拷贝时使用）
  Prototype deepCopy() const;
};

// 编译单元 - 一个模块的编译结果
struct CompiledChunk {
  std::string moduleName;           // 模块名
  Prototype mainProto;              // 主函数原型
  std::vector<std::string> exports; // 导出符号列表
  uint32_t version = 1;             // 字节码版本

  // 默认构造
  CompiledChunk() = default;

  // 移动构造/赋值（因为 Prototype 禁用了拷贝）
  CompiledChunk(CompiledChunk &&) = default;
  CompiledChunk &operator=(CompiledChunk &&) = default;

  // 禁用拷贝
  CompiledChunk(const CompiledChunk &) = delete;
  CompiledChunk &operator=(const CompiledChunk &) = delete;
};

struct DebugInfo {
  std::string name;     // 函数名
  std::string source;   // 源文件名
  std::string shortSrc; // 短源文件名
  int lineDefined;      // 起始行号
  int lastLineDefined;  // 结束行号
  int currentLine;
};

// 指令构建宏
inline constexpr Instruction MAKE_ABC(uint8_t op, uint8_t a, uint8_t b, uint8_t c, uint8_t k = 0) {
  return static_cast<uint32_t>(op & 0x7F) | (static_cast<uint32_t>(a) << 7) |
         (static_cast<uint32_t>(k & 0x01) << 15) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(c) << 24);
}

inline constexpr Instruction MAKE_ABx(uint8_t op, uint8_t a, uint32_t bx) {
  return static_cast<uint32_t>(op) | (static_cast<uint32_t>(a) << 7) | ((bx & 0x1FFFF) << 15);
}

inline constexpr Instruction MAKE_AsBx(uint8_t op, uint8_t a, int32_t sbx) {
  uint32_t bx = static_cast<uint32_t>(sbx + (0x1FFFF >> 1));
  return MAKE_ABx(op, a, bx);
}

} // namespace spt

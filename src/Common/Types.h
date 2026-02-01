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
  Class,
  Upvalue,
  Fiber,
  NativeObject,
  LightUserData
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
// 包含字节码、常量表、调试信息等。
//
// 生命周期管理（非 RAII，兼容 setjmp/longjmp）:
// ============================================================================
// 此结构分为两部分：
// 1. 编译时数据（std::vector/std::string）- 由编译器填充
// 2. 运行时数据（原始指针）- 由 VM::preparePrototype() 分配
//
// 重要：运行时数据必须通过 destroy() 显式释放
// 析构函数不会自动释放运行时资源。
//
// 典型生命周期：
//   Prototype proto;                    // 创建，运行时指针为 nullptr
//   compiler.compile(&proto);           // 填充编译时数据
//   vm.preparePrototype(&proto);        // 分配运行时数据
//   ... 执行 ...
//   Prototype::destroy(&proto);         // 显式释放运行时数据
//   // proto 析构时只释放 vector/string
// ============================================================================
struct Prototype {
  // === 编译时元数据（由编译器填充）===
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
  bool jitReady = false;

  // === 编译时数据（std::vector，用于序列化/编译）===
  std::vector<Instruction> code;        // 指令序列
  std::vector<ConstantValue> constants; // 常量表
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
  // 运行时数据（手动管理）
  // =========================================================================
  // 这些指针在 VM 加载 Prototype 时分配，用于运行时快速访问
  // 必须通过 destroy() 显式释放

  // 指令数组 (从 code vector 拷贝)
  Instruction *codePtr = nullptr;
  uint32_t codeCount = 0;

  // 常量表 (从 constants variant 转换为 Value)
  Value *k = nullptr;
  uint32_t kCount = 0;

  // Upvalue 描述数组 (从 upvalues vector 拷贝)
  UpvalueDesc *upvaluePtr = nullptr;

  Prototype **protoPtr = nullptr;
  uint32_t protoCount = 0;

  // =========================================================================
  // 运行时状态查询
  // =========================================================================

  bool hasRuntimeResources() const {
    return codePtr != nullptr || k != nullptr || upvaluePtr != nullptr || protoPtr != nullptr;
  }

  // =========================================================================
  // 生命周期管理 - 必须显式调用，不依赖构造/析构
  // =========================================================================

  // 初始化运行时指针为安全状态（幂等操作）
  static void init(Prototype *proto);

  // 销毁运行时数据（递归处理子 Prototype）
  static void destroy(Prototype *proto);

  // 完全重置（销毁运行时 + 清空编译时数据）
  static void reset(Prototype *proto);

  // =========================================================================
  // 构造与析构
  // =========================================================================

  Prototype() = default;

  // 析构函数 - 不释放运行时资源
  ~Prototype() = default;

  // 移动构造函数
  Prototype(Prototype &&other) noexcept;

  // 移动赋值运算符
  Prototype &operator=(Prototype &&other) noexcept;

  // 禁用拷贝
  Prototype(const Prototype &) = delete;
  Prototype &operator=(const Prototype &) = delete;

  // 深拷贝（仅编译时数据，运行时指针置空）
  Prototype deepCopy() const;
};

// ============================================================================
// CompiledChunk - 一个模块的编译结果
// ============================================================================
struct CompiledChunk {
  std::string moduleName;           // 模块名
  Prototype mainProto;              // 主函数原型
  std::vector<std::string> exports; // 导出符号列表
  uint32_t version = 1;             // 字节码版本

  // 默认构造
  CompiledChunk() = default;
  ~CompiledChunk() = default;

  CompiledChunk(CompiledChunk &&) = default;
  CompiledChunk &operator=(CompiledChunk &&) = default;

  CompiledChunk(const CompiledChunk &) = delete;
  CompiledChunk &operator=(const CompiledChunk &) = delete;

  void destroyRuntimeData() { Prototype::destroy(&mainProto); }

  bool hasRuntimeResources() const { return mainProto.hasRuntimeResources(); }
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
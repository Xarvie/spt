#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace spt {

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
  Upvalue
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

// 原型信息 - 函数/闭包的编译产物
struct Prototype {
  std::string name;         // 函数名 (调试用)
  std::string source;       // 源文件名
  int lineDefined = 0;      // 起始行号
  int lastLineDefined = 0;  // 结束行号
  uint8_t numParams = 0;    // 参数数量
  uint8_t numUpvalues = 0;  // UpValue 数量
  uint8_t maxStackSize = 0; // 最大栈深度
  bool isVararg = false;    // 是否可变参数

  std::vector<Instruction> code;        // 指令序列
  std::vector<ConstantValue> constants; // 常量表
  std::vector<int> lineInfo;            // 行号信息 (调试用)
  std::vector<Prototype> protos;        // 子函数原型
  uint8_t flags = FunctionFlag::FUNC_NONE;

  // UpValue 描述 - 用于闭包创建
  struct UpvalueDesc {
    uint8_t index; // 在父函数中的索引
    bool isLocal;  // true=父函数局部变量, false=父函数UpValue
  };
  std::vector<UpvalueDesc> upvalues;
};

// 编译单元 - 一个模块的编译结果
struct CompiledChunk {
  std::string moduleName;           // 模块名
  Prototype mainProto;              // 主函数原型
  std::vector<std::string> exports; // 导出符号列表
  uint32_t version = 1;             // 字节码版本
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

#pragma once

#include "Types.h"
#include <string>
#include <vector>

namespace spt {

// 字节码序列化器 - 支持热更新的二进制格式
class BytecodeSerializer {
public:
  // 序列化编译结果到二进制
  static std::vector<uint8_t> serialize(const CompiledChunk &chunk);

  // 从二进制反序列化
  static CompiledChunk deserialize(const std::vector<uint8_t> &data);

  // 写入文件
  static bool saveToFile(const CompiledChunk &chunk, const std::string &path);

  // 从文件加载
  static CompiledChunk loadFromFile(const std::string &path);

private:
  // 内部写入辅助
  class Writer {
  public:
    void writeU8(uint8_t v);
    void writeU16(uint16_t v);
    void writeU32(uint32_t v);
    void writeU64(uint64_t v);
    void writeI64(int64_t v);
    void writeF64(double v);
    void writeString(const std::string &s);
    void writeBytes(const void *data, size_t len);
    std::vector<uint8_t> finish();

  private:
    std::vector<uint8_t> buffer_;
  };

  // 内部读取辅助
  class Reader {
  public:
    explicit Reader(const std::vector<uint8_t> &data);
    uint8_t readU8();
    uint16_t readU16();
    uint32_t readU32();
    uint64_t readU64();
    int64_t readI64();
    double readF64();
    std::string readString();
    bool eof() const;

  private:
    const std::vector<uint8_t> &data_;
    size_t pos_ = 0;
  };

  static void writePrototype(Writer &w, const Prototype &proto);
  static Prototype readPrototype(Reader &r);
  static void writeConstant(Writer &w, const ConstantValue &val);
  static ConstantValue readConstant(Reader &r);
};

#include "OpCode.h"

class BytecodeDumper {
public:
  // 对外入口：打印整个 Chunk
  static void dump(const CompiledChunk& chunk);

private:
  // 递归打印原型及其子原型
  static void dumpPrototype(const Prototype& proto, const std::string& prefix = "");

  // 辅助函数：操作码转字符串
  static std::string opCodeToString(OpCode op);

  // 辅助函数：常量转字符串
  static std::string constantToString(const ConstantValue& val);

  // 辅助函数：获取指令格式 (用于决定如何打印操作数)
  enum class OpMode { iABC, iABx, iAsBx };
  static OpMode getOpMode(OpCode op);

  // 辅助函数：计算行号 (复用 VM 中的逻辑)
  static int getLine(const Prototype& proto, int pc);
};

} // namespace spt

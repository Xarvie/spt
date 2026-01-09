#include "BytecodeSerializer.h"
#include "OpCode.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace spt {

static constexpr uint32_t MAGIC = 0x58454C46;

void BytecodeSerializer::Writer::writeU8(uint8_t v) { buffer_.push_back(v); }

void BytecodeSerializer::Writer::writeU16(uint16_t v) {
  buffer_.push_back(v & 0xFF);
  buffer_.push_back((v >> 8) & 0xFF);
}

void BytecodeSerializer::Writer::writeU32(uint32_t v) {
  buffer_.push_back(v & 0xFF);
  buffer_.push_back((v >> 8) & 0xFF);
  buffer_.push_back((v >> 16) & 0xFF);
  buffer_.push_back((v >> 24) & 0xFF);
}

void BytecodeSerializer::Writer::writeU64(uint64_t v) {
  writeU32(static_cast<uint32_t>(v));
  writeU32(static_cast<uint32_t>(v >> 32));
}

void BytecodeSerializer::Writer::writeI64(int64_t v) { writeU64(static_cast<uint64_t>(v)); }

void BytecodeSerializer::Writer::writeF64(double v) {
  uint64_t bits;
  std::memcpy(&bits, &v, sizeof(double));
  writeU64(bits);
}

void BytecodeSerializer::Writer::writeString(const std::string &s) {
  writeU32(static_cast<uint32_t>(s.size()));
  buffer_.insert(buffer_.end(), s.begin(), s.end());
}

void BytecodeSerializer::Writer::writeBytes(const void *data, size_t len) {
  auto *p = static_cast<const uint8_t *>(data);
  buffer_.insert(buffer_.end(), p, p + len);
}

std::vector<uint8_t> BytecodeSerializer::Writer::finish() { return std::move(buffer_); }

BytecodeSerializer::Reader::Reader(const std::vector<uint8_t> &data) : data_(data) {}

uint8_t BytecodeSerializer::Reader::readU8() {
  if (pos_ >= data_.size())
    throw std::runtime_error("Unexpected EOF");
  return data_[pos_++];
}

uint16_t BytecodeSerializer::Reader::readU16() {
  uint16_t v = readU8();
  v |= static_cast<uint16_t>(readU8()) << 8;
  return v;
}

uint32_t BytecodeSerializer::Reader::readU32() {
  uint32_t v = readU8();
  v |= static_cast<uint32_t>(readU8()) << 8;
  v |= static_cast<uint32_t>(readU8()) << 16;
  v |= static_cast<uint32_t>(readU8()) << 24;
  return v;
}

uint64_t BytecodeSerializer::Reader::readU64() {
  uint64_t lo = readU32();
  uint64_t hi = readU32();
  return lo | (hi << 32);
}

int64_t BytecodeSerializer::Reader::readI64() { return static_cast<int64_t>(readU64()); }

double BytecodeSerializer::Reader::readF64() {
  uint64_t bits = readU64();
  double v;
  std::memcpy(&v, &bits, sizeof(double));
  return v;
}

std::string BytecodeSerializer::Reader::readString() {
  uint32_t len = readU32();
  if (pos_ + len > data_.size())
    throw std::runtime_error("String overflow");
  std::string s(data_.begin() + pos_, data_.begin() + pos_ + len);
  pos_ += len;
  return s;
}

bool BytecodeSerializer::Reader::eof() const { return pos_ >= data_.size(); }

void BytecodeSerializer::writeConstant(Writer &w, const ConstantValue &val) {
  w.writeU8(static_cast<uint8_t>(val.index()));
  std::visit(
      [&w](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
        } else if constexpr (std::is_same_v<T, bool>) {
          w.writeU8(arg ? 1 : 0);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          w.writeI64(arg);
        } else if constexpr (std::is_same_v<T, double>) {
          w.writeF64(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
          w.writeString(arg);
        }
      },
      val);
}

ConstantValue BytecodeSerializer::readConstant(Reader &r) {
  uint8_t type = r.readU8();
  switch (type) {
  case 0:
    return nullptr;
  case 1:
    return r.readU8() != 0;
  case 2:
    return r.readI64();
  case 3:
    return r.readF64();
  case 4:
    return r.readString();
  default:
    throw std::runtime_error("Unknown constant type");
  }
}

void BytecodeSerializer::writePrototype(Writer &w, const Prototype &proto) {
  w.writeString(proto.name);
  w.writeString(proto.source);
  w.writeString(proto.short_src);
  w.writeU32(static_cast<uint32_t>(proto.lineDefined));
  w.writeU32(static_cast<uint32_t>(proto.lastLineDefined));
  w.writeU8(proto.numParams);
  w.writeU8(proto.numUpvalues);
  w.writeU8(proto.maxStackSize);
  w.writeU8(proto.isVararg ? 1 : 0);

  w.writeU32(static_cast<uint32_t>(proto.code.size()));
  for (auto inst : proto.code)
    w.writeU32(inst);

  w.writeU32(static_cast<uint32_t>(proto.constants.size()));
  for (const auto &c : proto.constants)
    writeConstant(w, c);

  w.writeU32(static_cast<uint32_t>(proto.lineInfo.size()));
  for (auto line : proto.lineInfo)
    w.writeU32(static_cast<uint32_t>(line));

  w.writeU8(static_cast<uint8_t>(proto.upvalues.size()));
  for (const auto &uv : proto.upvalues) {
    w.writeU8(uv.index);
    w.writeU8(uv.isLocal ? 1 : 0);
  }

  w.writeU32(static_cast<uint32_t>(proto.protos.size()));
  for (const auto &p : proto.protos)
    writePrototype(w, p);
}

Prototype BytecodeSerializer::readPrototype(Reader &r) {
  Prototype proto;
  proto.name = r.readString();
  proto.source = r.readString();
  proto.short_src = r.readString();
  proto.lineDefined = static_cast<int>(r.readU32());
  proto.lastLineDefined = static_cast<int>(r.readU32());
  proto.numParams = r.readU8();
  proto.numUpvalues = r.readU8();
  proto.maxStackSize = r.readU8();
  proto.isVararg = r.readU8() != 0;

  uint32_t codeSize = r.readU32();
  proto.code.reserve(codeSize);
  for (uint32_t i = 0; i < codeSize; ++i)
    proto.code.push_back(r.readU32());

  uint32_t constSize = r.readU32();
  proto.constants.reserve(constSize);
  for (uint32_t i = 0; i < constSize; ++i)
    proto.constants.push_back(readConstant(r));

  uint32_t lineSize = r.readU32();
  proto.lineInfo.reserve(lineSize);
  for (uint32_t i = 0; i < lineSize; ++i)
    proto.lineInfo.push_back(static_cast<int>(r.readU32()));

  uint8_t uvSize = r.readU8();
  proto.upvalues.reserve(uvSize);
  for (uint8_t i = 0; i < uvSize; ++i) {
    Prototype::UpvalueDesc uv;
    uv.index = r.readU8();
    uv.isLocal = r.readU8() != 0;
    proto.upvalues.push_back(uv);
  }

  uint32_t protoSize = r.readU32();
  proto.protos.reserve(protoSize);
  for (uint32_t i = 0; i < protoSize; ++i)
    proto.protos.push_back(readPrototype(r));

  return proto;
}

std::vector<uint8_t> BytecodeSerializer::serialize(const CompiledChunk &chunk) {
  Writer w;
  w.writeU32(MAGIC);
  w.writeU32(chunk.version);
  w.writeString(chunk.moduleName);

  w.writeU32(static_cast<uint32_t>(chunk.exports.size()));
  for (const auto &exp : chunk.exports)
    w.writeString(exp);

  writePrototype(w, chunk.mainProto);
  return w.finish();
}

CompiledChunk BytecodeSerializer::deserialize(const std::vector<uint8_t> &data) {
  Reader r(data);
  uint32_t magic = r.readU32();
  if (magic != MAGIC)
    throw std::runtime_error("Invalid bytecode magic");

  CompiledChunk chunk;
  chunk.version = r.readU32();
  chunk.moduleName = r.readString();

  uint32_t exportCount = r.readU32();
  chunk.exports.reserve(exportCount);
  for (uint32_t i = 0; i < exportCount; ++i)
    chunk.exports.push_back(r.readString());

  chunk.mainProto = readPrototype(r);
  return chunk;
}

bool BytecodeSerializer::saveToFile(const CompiledChunk &chunk, const std::string &path) {
  auto data = serialize(chunk);
  std::ofstream out(path, std::ios::binary);
  if (!out)
    return false;
  out.write(reinterpret_cast<const char *>(data.data()), data.size());
  return out.good();
}

CompiledChunk BytecodeSerializer::loadFromFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    throw std::runtime_error("Cannot open file: " + path);

  auto size = in.tellg();
  in.seekg(0);

  std::vector<uint8_t> data(size);
  in.read(reinterpret_cast<char *>(data.data()), size);
  return deserialize(data);
}

void BytecodeDumper::dump(const CompiledChunk &chunk) {
  std::cout << "== Dump Module: " << chunk.moduleName << " ==\n";
  dumpPrototype(chunk.mainProto);
}

void BytecodeDumper::dumpPrototype(const Prototype &proto, const std::string &prefix) {
  std::string funcName = proto.name.empty() ? "<anonymous>" : proto.name;
  std::string source = proto.source.empty() ? "=?" : proto.source;

  std::cout << "\n"
            << prefix << "function " << funcName << " (" << source << ":" << proto.lineDefined
            << "-" << proto.lastLineDefined << ")\n";

  std::cout << prefix << "params: " << (int)proto.numParams
            << ", upvalues: " << (int)proto.numUpvalues << ", slots: " << (int)proto.maxStackSize
            << ", vararg: " << (proto.isVararg ? "yes" : "no") << "\n";

  for (size_t i = 0; i < proto.code.size(); ++i) {
    Instruction inst = proto.code[i];
    OpCode op = GET_OPCODE(inst);
    int line = getLine(proto, static_cast<int>(i));

    std::cout << prefix << "\t" << "[" << std::setw(3) << i << "] " << "[" << std::setw(3) << line
              << "] " << std::setw(14) << std::left << opCodeToString(op) << " ";

    OpMode mode = getOpMode(op);
    int a = GETARG_A(inst);
    int b = GETARG_B(inst);
    int c = GETARG_C(inst);
    int bx = GETARG_Bx(inst);
    int sbx = GETARG_sBx(inst);

    std::stringstream comment;

    switch (mode) {
    case OpMode::iABC:
      std::cout << std::setw(4) << a << " " << std::setw(4) << b << " " << std::setw(4) << c;

      if (op == OpCode::OP_GETFIELD || op == OpCode::OP_SETFIELD) {

        if (c < proto.constants.size()) {
          comment << "; key=" << constantToString(proto.constants[c]);
        }
      } else if (op == OpCode::OP_INVOKE) {

        if (c < proto.constants.size()) {
          comment << "; method=" << constantToString(proto.constants[c]);
        }
      }
      break;

    case OpMode::iABx:
      std::cout << std::setw(4) << a << " " << std::setw(9) << bx;

      if (op == OpCode::OP_LOADK) {
        if (bx < proto.constants.size()) {
          comment << "; " << constantToString(proto.constants[bx]);
        }
      } else if (op == OpCode::OP_NEWCLASS) {
        if (bx < proto.constants.size()) {
          comment << "; class_name=" << constantToString(proto.constants[bx]);
        }
      } else if (op == OpCode::OP_CLOSURE) {
        if (bx < proto.protos.size()) {

          const auto &sub = proto.protos[bx];
          comment << "; " << (sub.name.empty() ? "<anonymous>" : sub.name);
        }
      }
      break;

    case OpMode::iAsBx:
      std::cout << std::setw(4) << a << " " << std::setw(9) << sbx;

      if (op == OpCode::OP_JMP) {
        int dest = static_cast<int>(i) + sbx + 1;
        comment << "; to [" << dest << "]";
      }
      break;
    }

    std::string commentStr = comment.str();
    if (!commentStr.empty()) {
      std::cout << "\t" << commentStr;
    }
    std::cout << "\n";
  }

  if (!proto.constants.empty()) {
    std::cout << prefix << "  Constants (" << proto.constants.size() << "):\n";
    for (size_t i = 0; i < proto.constants.size(); ++i) {
      std::cout << prefix << "    [" << i << "] " << constantToString(proto.constants[i]) << "\n";
    }
  }

  for (const auto &subProto : proto.protos) {
    dumpPrototype(subProto, prefix + "  ");
  }
}

std::string BytecodeDumper::constantToString(const ConstantValue &val) {
  return std::visit(
      [](auto &&arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          return "nil";
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
          return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
          return "\"" + arg + "\"";
        }
        return "?";
      },
      val);
}

int BytecodeDumper::getLine(const Prototype &proto, int pc) {
  if (proto.lineInfo.empty())
    return 0;

  int line = proto.absLineInfo.front().line;
  auto abs_line_info = std::lower_bound(proto.absLineInfo.begin(), proto.absLineInfo.end(), pc,
                                        [](const auto &lhs, int p) { return lhs.pc < p; });

  int basePC = 0;
  if (abs_line_info != proto.absLineInfo.end()) {
    if (abs_line_info != proto.absLineInfo.begin()) {
      --abs_line_info;
    }
    basePC = abs_line_info->pc;
    line = abs_line_info->line;
  }

  while (basePC < pc && basePC < (int)proto.lineInfo.size()) {
    line += proto.lineInfo[basePC];
    basePC++;
  }
  return line;
}

BytecodeDumper::OpMode BytecodeDumper::getOpMode(OpCode op) {
  switch (op) {
  case OpCode::OP_LOADK:
  case OpCode::OP_NEWCLASS:
  case OpCode::OP_CLOSURE:
  case OpCode::OP_IMPORT:
    return OpMode::iABx;

  case OpCode::OP_JMP:
    return OpMode::iAsBx;

  default:
    return OpMode::iABC;
  }
}

std::string BytecodeDumper::opCodeToString(OpCode op) {
  switch (op) {
  case OpCode::OP_MOVE:
    return "MOVE";
  case OpCode::OP_LOADK:
    return "LOADK";
  case OpCode::OP_LOADBOOL:
    return "LOADBOOL";
  case OpCode::OP_LOADNIL:
    return "LOADNIL";
  case OpCode::OP_NEWLIST:
    return "NEWLIST";
  case OpCode::OP_NEWMAP:
    return "NEWMAP";
  case OpCode::OP_GETINDEX:
    return "GETINDEX";
  case OpCode::OP_SETINDEX:
    return "SETINDEX";
  case OpCode::OP_GETFIELD:
    return "GETFIELD";
  case OpCode::OP_SETFIELD:
    return "SETFIELD";
  case OpCode::OP_NEWCLASS:
    return "NEWCLASS";
  case OpCode::OP_NEWOBJ:
    return "NEWOBJ";
  case OpCode::OP_GETUPVAL:
    return "GETUPVAL";
  case OpCode::OP_SETUPVAL:
    return "SETUPVAL";
  case OpCode::OP_CLOSURE:
    return "CLOSURE";
  case OpCode::OP_CLOSE_UPVALUE:
    return "CLOSE_UPVAL";
  case OpCode::OP_ADD:
    return "ADD";
  case OpCode::OP_SUB:
    return "SUB";
  case OpCode::OP_MUL:
    return "MUL";
  case OpCode::OP_DIV:
    return "DIV";
  case OpCode::OP_MOD:
    return "MOD";
  case OpCode::OP_UNM:
    return "UNM";
  case OpCode::OP_JMP:
    return "JMP";
  case OpCode::OP_EQ:
    return "EQ";
  case OpCode::OP_LT:
    return "LT";
  case OpCode::OP_LE:
    return "LE";
  case OpCode::OP_TEST:
    return "TEST";
  case OpCode::OP_CALL:
    return "CALL";
  case OpCode::OP_INVOKE:
    return "INVOKE";
  case OpCode::OP_RETURN:
    return "RETURN";
  case OpCode::OP_IMPORT:
    return "IMPORT";
  case OpCode::OP_IMPORT_FROM:
    return "IMPORT_FROM";
  case OpCode::OP_EXPORT:
    return "EXPORT";
  case OpCode::OP_DEFER:
    return "OP_DEFER";

  case OpCode::OP_ADDI:
    return "ADDI";
  case OpCode::OP_EQK:
    return "EQK";
  case OpCode::OP_EQI:
    return "EQI";
  case OpCode::OP_LTI:
    return "LTI";
  case OpCode::OP_LEI:
    return "LEI";
  case OpCode::OP_FORPREP:
    return "FORPREP";
  case OpCode::OP_FORLOOP:
    return "FORLOOP";

  default:
    return "UNKNOWN";
  }
}

} // namespace spt

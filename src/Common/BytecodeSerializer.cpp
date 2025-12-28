#include "BytecodeSerializer.h"
#include <cstring>
#include <fstream>
#include <stdexcept>

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

} // namespace spt

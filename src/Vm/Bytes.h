#pragma once

#include "Value.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace spt {

class VM;
struct StringObject;
struct SymbolTable;

// ============================================================================
// BytesObject - 可变长度字节数组
// ============================================================================
struct BytesObject : GCObject {
  std::vector<uint8_t> data;

  BytesObject() { type = ValueType::Bytes; }

  explicit BytesObject(size_t size) : data(size, 0) { type = ValueType::Bytes; }

  // === 基础访问 ===
  size_t length() const { return data.size(); }

  uint8_t get(int64_t index) const {
    if (index < 0)
      index += static_cast<int64_t>(data.size());
    if (index < 0 || index >= static_cast<int64_t>(data.size()))
      return 0;
    return data[static_cast<size_t>(index)];
  }

  void set(int64_t index, uint8_t value) {
    if (index < 0)
      index += static_cast<int64_t>(data.size());
    if (index >= 0 && index < static_cast<int64_t>(data.size()))
      data[static_cast<size_t>(index)] = value;
  }

  // === 容器操作 ===
  void push(uint8_t byte) { data.push_back(byte); }

  int pop() {
    if (data.empty())
      return -1; // 表示 nil
    uint8_t val = data.back();
    data.pop_back();
    return val;
  }

  void clear() { data.clear(); }

  void resize(size_t newSize) { data.resize(newSize, 0); }

  // === 二进制读取 (Little Endian 默认) ===
  template <typename T> T read(size_t offset, bool bigEndian = false) const {
    if (offset + sizeof(T) > data.size())
      return T{};
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    if (bigEndian)
      value = swapEndian(value);
    return value;
  }

  int8_t readInt8(size_t offset) const {
    if (offset >= data.size())
      return 0;
    return static_cast<int8_t>(data[offset]);
  }

  uint8_t readUInt8(size_t offset) const {
    if (offset >= data.size())
      return 0;
    return data[offset];
  }

  int16_t readInt16(size_t offset, bool bigEndian = false) const {
    return read<int16_t>(offset, bigEndian);
  }

  uint16_t readUInt16(size_t offset, bool bigEndian = false) const {
    return read<uint16_t>(offset, bigEndian);
  }

  int32_t readInt32(size_t offset, bool bigEndian = false) const {
    return read<int32_t>(offset, bigEndian);
  }

  uint32_t readUInt32(size_t offset, bool bigEndian = false) const {
    return read<uint32_t>(offset, bigEndian);
  }

  float readFloat(size_t offset, bool bigEndian = false) const {
    return read<float>(offset, bigEndian);
  }

  double readDouble(size_t offset, bool bigEndian = false) const {
    return read<double>(offset, bigEndian);
  }

  std::string readString(size_t offset, size_t byteLen) const {
    if (offset >= data.size())
      return "";
    size_t actualLen = std::min(byteLen, data.size() - offset);
    return std::string(reinterpret_cast<const char *>(data.data() + offset), actualLen);
  }

  // === 二进制写入 (Little Endian 默认) ===
  template <typename T> bool write(size_t offset, T value, bool bigEndian = false) {
    if (offset + sizeof(T) > data.size())
      return false;
    if (bigEndian)
      value = swapEndian(value);
    std::memcpy(data.data() + offset, &value, sizeof(T));
    return true;
  }

  bool writeInt8(size_t offset, int8_t value) {
    if (offset >= data.size())
      return false;
    data[offset] = static_cast<uint8_t>(value);
    return true;
  }

  bool writeUInt8(size_t offset, uint8_t value) {
    if (offset >= data.size())
      return false;
    data[offset] = value;
    return true;
  }

  bool writeInt16(size_t offset, int16_t value, bool bigEndian = false) {
    return write<int16_t>(offset, value, bigEndian);
  }

  bool writeUInt16(size_t offset, uint16_t value, bool bigEndian = false) {
    return write<uint16_t>(offset, value, bigEndian);
  }

  bool writeInt32(size_t offset, int32_t value, bool bigEndian = false) {
    return write<int32_t>(offset, value, bigEndian);
  }

  bool writeUInt32(size_t offset, uint32_t value, bool bigEndian = false) {
    return write<uint32_t>(offset, value, bigEndian);
  }

  bool writeFloat(size_t offset, float value, bool bigEndian = false) {
    return write<float>(offset, value, bigEndian);
  }

  bool writeDouble(size_t offset, double value, bool bigEndian = false) {
    return write<double>(offset, value, bigEndian);
  }

  size_t writeString(size_t offset, std::string_view str) {
    if (offset >= data.size())
      return 0;
    size_t canWrite = std::min(str.size(), data.size() - offset);
    std::memcpy(data.data() + offset, str.data(), canWrite);
    return canWrite;
  }

  // === 工具方法 ===
  std::string toString() const {
    // 尝试 UTF-8 解码，无效字节用替换字符
    std::string result;
    result.reserve(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
      uint8_t c = data[i];
      if (c < 0x80) {
        result += static_cast<char>(c);
      } else {
        // 简化处理：直接复制字节
        result += static_cast<char>(c);
      }
    }
    return result;
  }

  std::string toHex() const {
    static const char hexChars[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(data.size() * 2);
    for (uint8_t byte : data) {
      result += hexChars[(byte >> 4) & 0x0F];
      result += hexChars[byte & 0x0F];
    }
    return result;
  }

  void fill(uint8_t value, size_t start, size_t end) {
    if (start >= data.size())
      return;
    end = std::min(end, data.size());
    for (size_t i = start; i < end; ++i)
      data[i] = value;
  }

private:
  // 字节序交换辅助函数
  template <typename T> static T swapEndian(T value) {
    static_assert(std::is_arithmetic_v<T>, "Only arithmetic types supported");

    union {
      T val;
      uint8_t bytes[sizeof(T)];
    } src, dst;

    src.val = value;
    for (size_t i = 0; i < sizeof(T); ++i)
      dst.bytes[i] = src.bytes[sizeof(T) - 1 - i];
    return dst.val;
  }
};

// ============================================================================
// Bytes 标准库加载
// ============================================================================
class SptBytes {
public:
  static void load(VM *vm);
};

} // namespace spt
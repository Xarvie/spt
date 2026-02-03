#include "Bytes.h"
#include "Object.h"
#include "StringPool.h"
#include "VM.h"
#include <algorithm>
#include <cctype>

namespace spt {

static int bytesBoundMethodDispatcher(VM *vm, Closure *self, int argc, Value *argv) {
  Value fnVal = self->getNativeUpvalue(0);
  if (!fnVal.isInt()) {
    vm->throwError(Value::object(vm->allocateString("Internal error: invalid bound method")));
    return 0;
  }

  int64_t fnPtr = fnVal.asInt();
  auto fn = reinterpret_cast<BuiltinMethodDesc::MethodFn>(fnPtr);

  Value result = fn(vm, self->receiver, argc, argv);
  vm->push(result);
  return 1;
}

static Value createBytesBoundNative(VM *vm, Value receiver, StringObject *name,
                                    BuiltinMethodDesc::MethodFn fn, int arity) {
  vm->protect(receiver);
  vm->protect(Value::object(name));

  Closure *native = vm->gc().allocateNativeClosure(1);
  native->name = name;
  native->arity = arity;
  native->receiver = receiver;
  native->function = bytesBoundMethodDispatcher;
  native->setNativeUpvalue(0, Value::integer(reinterpret_cast<int64_t>(fn)));

  vm->unprotect(2);
  return Value::object(native);
}

static Value bytesPush(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());

  if (argc < 1 || !argv[0].isInt()) {
    vm->throwError(Value::object(vm->allocateString("Expected integer")));
    return Value::nil();
  }

  int64_t val = argv[0].asInt();
  bytes->push(static_cast<uint8_t>(val & 0xFF));
  return Value::nil();
}

static Value bytesPop(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());

  int result = bytes->pop();
  if (result < 0)
    return Value::nil();
  return Value::integer(result);
}

static Value bytesClear(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes())
    return Value::nil();
  static_cast<BytesObject *>(receiver.asGC())->clear();
  return Value::nil();
}

static Value bytesResize(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());

  if (argc < 1 || !argv[0].isInt()) {
    vm->throwError(Value::object(vm->allocateString("Expected integer")));
    return Value::nil();
  }

  int64_t newSize = argv[0].asInt();
  if (newSize < 0) {
    vm->throwError(Value::object(vm->allocateString("Size must be >= 0")));
    return Value::nil();
  }

  bytes->resize(static_cast<size_t>(newSize));
  return Value::nil();
}

static Value bytesSlice(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2)
    return Value::nil();

  if (!argv[0].isInt() || !argv[1].isInt()) {
    vm->throwError(Value::object(vm->allocateString("Expected integers")));
    return Value::nil();
  }

  int64_t start = argv[0].asInt();
  int64_t end = argv[1].asInt();

  vm->protect(receiver);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  int64_t len = static_cast<int64_t>(bytes->data.size());

  if (start < 0)
    start = std::max(int64_t(0), len + start);
  if (end < 0)
    end = std::max(int64_t(0), len + end);
  start = std::clamp(start, int64_t(0), len);
  end = std::clamp(end, int64_t(0), len);

  BytesObject *result = vm->gc().allocateBytes(0);
  vm->protect(Value::object(result));

  bytes = static_cast<BytesObject *>(receiver.asGC());

  if (end > start) {
    result->data.assign(bytes->data.begin() + start, bytes->data.begin() + end);
  }

  vm->unprotect(2);
  return Value::object(result);
}

static Value bytesFill(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes())
    return Value::nil();

  if (argc < 3 || !argv[0].isInt() || !argv[1].isInt() || !argv[2].isInt()) {
    vm->throwError(Value::object(vm->allocateString("Expected integers")));
    return Value::nil();
  }

  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  uint8_t value = static_cast<uint8_t>(argv[0].asInt() & 0xFF);
  int64_t start = argv[1].asInt();
  int64_t end = argv[2].asInt();

  if (start < 0)
    start = 0;
  if (end < 0)
    end = 0;

  bytes->fill(value, static_cast<size_t>(start), static_cast<size_t>(end));
  return Value::nil();
}

static Value bytesReadInt8(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 1 || !argv[0].isInt())
    return Value::integer(0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  if (offset >= bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::integer(0);
  }
  return Value::integer(bytes->readInt8(offset));
}

static Value bytesReadUInt8(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 1 || !argv[0].isInt())
    return Value::integer(0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  if (offset >= bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::integer(0);
  }
  return Value::integer(bytes->readUInt8(offset));
}

static Value bytesReadInt16(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 1 || !argv[0].isInt())
    return Value::integer(0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  bool bigEndian = (argc >= 2 && argv[1].isBool()) ? argv[1].asBool() : false;
  if (offset + 2 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::integer(0);
  }
  return Value::integer(bytes->readInt16(offset, bigEndian));
}

static Value bytesReadUInt16(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 1 || !argv[0].isInt())
    return Value::integer(0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  bool bigEndian = (argc >= 2 && argv[1].isBool()) ? argv[1].asBool() : false;
  if (offset + 2 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::integer(0);
  }
  return Value::integer(bytes->readUInt16(offset, bigEndian));
}

static Value bytesReadInt32(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 1 || !argv[0].isInt())
    return Value::integer(0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  bool bigEndian = (argc >= 2 && argv[1].isBool()) ? argv[1].asBool() : false;
  if (offset + 4 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::integer(0);
  }
  return Value::integer(bytes->readInt32(offset, bigEndian));
}

static Value bytesReadUInt32(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 1 || !argv[0].isInt())
    return Value::integer(0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  bool bigEndian = (argc >= 2 && argv[1].isBool()) ? argv[1].asBool() : false;
  if (offset + 4 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::integer(0);
  }
  return Value::integer(static_cast<int64_t>(bytes->readUInt32(offset, bigEndian)));
}

static Value bytesReadFloat(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 1 || !argv[0].isInt())
    return Value::number(0.0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  bool bigEndian = (argc >= 2 && argv[1].isBool()) ? argv[1].asBool() : false;
  if (offset + 4 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::number(0.0);
  }
  return Value::number(bytes->readFloat(offset, bigEndian));
}

static Value bytesReadDouble(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 1 || !argv[0].isInt())
    return Value::number(0.0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  bool bigEndian = (argc >= 2 && argv[1].isBool()) ? argv[1].asBool() : false;
  if (offset + 8 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::number(0.0);
  }
  return Value::number(bytes->readDouble(offset, bigEndian));
}

static Value bytesReadString(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isInt())
    return Value::object(vm->allocateString(""));
  int64_t lenArg = argv[1].asInt();
  if (lenArg < 0) {
    return Value::object(vm->allocateString(""));
  }
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  size_t byteLen = static_cast<size_t>(argv[1].asInt());
  std::string result = bytes->readString(offset, byteLen);
  return Value::object(vm->allocateString(result));
}

static Value bytesWriteInt8(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isInt())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  int8_t value = static_cast<int8_t>(argv[1].asInt());
  if (offset >= bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::nil();
  }
  bytes->writeInt8(offset, value);
  return Value::nil();
}

static Value bytesWriteUInt8(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isInt())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  uint8_t value = static_cast<uint8_t>(argv[1].asInt() & 0xFF);
  if (offset >= bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::nil();
  }
  bytes->writeUInt8(offset, value);
  return Value::nil();
}

static Value bytesWriteInt16(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isInt())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  int16_t value = static_cast<int16_t>(argv[1].asInt());
  bool bigEndian = (argc >= 3 && argv[2].isBool()) ? argv[2].asBool() : false;
  if (offset + 2 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::nil();
  }
  bytes->writeInt16(offset, value, bigEndian);
  return Value::nil();
}

static Value bytesWriteUInt16(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isInt())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  uint16_t value = static_cast<uint16_t>(argv[1].asInt());
  bool bigEndian = (argc >= 3 && argv[2].isBool()) ? argv[2].asBool() : false;
  if (offset + 2 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::nil();
  }
  bytes->writeUInt16(offset, value, bigEndian);
  return Value::nil();
}

static Value bytesWriteInt32(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isInt())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  int32_t value = static_cast<int32_t>(argv[1].asInt());
  bool bigEndian = (argc >= 3 && argv[2].isBool()) ? argv[2].asBool() : false;
  if (offset + 4 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::nil();
  }
  bytes->writeInt32(offset, value, bigEndian);
  return Value::nil();
}

static Value bytesWriteUInt32(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isInt())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  uint32_t value = static_cast<uint32_t>(argv[1].asInt());
  bool bigEndian = (argc >= 3 && argv[2].isBool()) ? argv[2].asBool() : false;
  if (offset + 4 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::nil();
  }
  bytes->writeUInt32(offset, value, bigEndian);
  return Value::nil();
}

static Value bytesWriteFloat(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isNumber())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  float value = static_cast<float>(argv[1].asNumber());
  bool bigEndian = (argc >= 3 && argv[2].isBool()) ? argv[2].asBool() : false;
  if (offset + 4 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::nil();
  }
  bytes->writeFloat(offset, value, bigEndian);
  return Value::nil();
}

static Value bytesWriteDouble(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isNumber())
    return Value::nil();
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  double value = argv[1].asNumber();
  bool bigEndian = (argc >= 3 && argv[2].isBool()) ? argv[2].asBool() : false;
  if (offset + 8 > bytes->data.size()) {
    vm->throwError(Value::object(vm->allocateString("Index out of bounds")));
    return Value::nil();
  }
  bytes->writeDouble(offset, value, bigEndian);
  return Value::nil();
}

static Value bytesWriteString(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes() || argc < 2 || !argv[0].isInt() || !argv[1].isString())
    return Value::integer(0);
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  size_t offset = static_cast<size_t>(argv[0].asInt());
  auto *str = static_cast<StringObject *>(argv[1].asGC());
  size_t written = bytes->writeString(offset, str->view());
  return Value::integer(static_cast<int64_t>(written));
}

static Value bytesToString(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes())
    return Value::object(vm->allocateString(""));
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  return Value::object(vm->allocateString(bytes->toString()));
}

static Value bytesToHex(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isBytes())
    return Value::object(vm->allocateString(""));
  auto *bytes = static_cast<BytesObject *>(receiver.asGC());
  return Value::object(vm->allocateString(bytes->toHex()));
}

static int bytesNew(VM *vm, Closure *self, int argc, Value *argv) {
  if (argc < 1 || !argv[0].isInt()) {
    vm->throwError(Value::object(vm->allocateString("Expected integer size")));
    return 0;
  }

  int64_t size = argv[0].asInt();
  if (size < 0) {
    vm->throwError(Value::object(vm->allocateString("Size must be >= 0")));
    return 0;
  }

  BytesObject *bytes = vm->gc().allocateBytes(static_cast<size_t>(size));
  vm->protect(Value::object(bytes));
  vm->push(Value::object(bytes));
  vm->unprotect(1);
  return 1;
}

static int bytesFromList(VM *vm, Closure *self, int argc, Value *argv) {
  if (argc < 1 || !argv[0].isList()) {
    vm->throwError(Value::object(vm->allocateString("Expected list")));
    return 0;
  }

  Value listVal = argv[0];
  vm->protect(listVal);
  auto *list = static_cast<ListObject *>(listVal.asGC());

  BytesObject *bytes = vm->gc().allocateBytes(list->elements.size());
  vm->protect(Value::object(bytes));

  list = static_cast<ListObject *>(listVal.asGC());

  for (size_t i = 0; i < list->elements.size(); ++i) {
    Value elem = list->elements[i];
    if (!elem.isInt()) {
      vm->unprotect(2);
      vm->throwError(Value::object(vm->allocateString("List elements must be integers")));
      return 0;
    }
    bytes->data[i] = static_cast<uint8_t>(elem.asInt() & 0xFF);
  }

  vm->unprotect(2);
  vm->push(Value::object(bytes));
  return 1;
}

static int bytesFromStr(VM *vm, Closure *self, int argc, Value *argv) {
  if (argc < 1 || !argv[0].isString()) {
    vm->throwError(Value::object(vm->allocateString("Expected string")));
    return 0;
  }

  auto *str = static_cast<StringObject *>(argv[0].asGC());
  std::string_view sv = str->view();

  BytesObject *bytes = vm->gc().allocateBytes(sv.size());
  vm->protect(Value::object(bytes));

  std::memcpy(bytes->data.data(), sv.data(), sv.size());

  vm->push(Value::object(bytes));
  vm->unprotect(1);
  return 1;
}

static int bytesFromHex(VM *vm, Closure *self, int argc, Value *argv) {
  if (argc < 1 || !argv[0].isString()) {
    vm->throwError(Value::object(vm->allocateString("Expected hex string")));
    return 0;
  }

  auto *str = static_cast<StringObject *>(argv[0].asGC());
  std::string_view sv = str->view();

  std::string cleaned;
  cleaned.reserve(sv.size());
  for (char c : sv) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      cleaned += c;
    }
  }

  if (cleaned.size() % 2 != 0) {
    vm->throwError(Value::object(vm->allocateString("Hex string must have even length")));
    return 0;
  }

  auto hexCharToInt = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    return -1;
  };

  BytesObject *bytes = vm->gc().allocateBytes(cleaned.size() / 2);
  vm->protect(Value::object(bytes));

  for (size_t i = 0; i < cleaned.size(); i += 2) {
    int hi = hexCharToInt(cleaned[i]);
    int lo = hexCharToInt(cleaned[i + 1]);
    if (hi < 0 || lo < 0) {
      vm->unprotect(1);
      vm->throwError(Value::object(vm->allocateString("Invalid hex character")));
      return 0;
    }
    bytes->data[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
  }

  vm->push(Value::object(bytes));
  vm->unprotect(1);
  return 1;
}

static void addStaticMethod(VM *vm, ClassObject *bytesClass, StringObject *name, NativeFn fn,
                            int arity) {
  Closure *native = vm->gc().allocateNativeClosure(0);
  vm->protect(Value::object(native));

  native->name = name;
  native->function = fn;
  native->arity = arity;
  native->receiver = Value::nil();

  vm->unprotect(1);

  bytesClass->statics[native->name] = Value::object(native);
}

static void registerBytesMethods(SymbolTable &syms) {

  syms.bytesMethods[syms.push] = {bytesPush, 1};
  syms.bytesMethods[syms.pop] = {bytesPop, 0};
  syms.bytesMethods[syms.clear] = {bytesClear, 0};
  syms.bytesMethods[syms.resize] = {bytesResize, 1};
  syms.bytesMethods[syms.slice] = {bytesSlice, 2};
  syms.bytesMethods[syms.fill] = {bytesFill, 3};

  syms.bytesMethods[syms.readInt8] = {bytesReadInt8, 1};
  syms.bytesMethods[syms.readUInt8] = {bytesReadUInt8, 1};
  syms.bytesMethods[syms.readInt16] = {bytesReadInt16, -1};
  syms.bytesMethods[syms.readUInt16] = {bytesReadUInt16, -1};
  syms.bytesMethods[syms.readInt32] = {bytesReadInt32, -1};
  syms.bytesMethods[syms.readUInt32] = {bytesReadUInt32, -1};
  syms.bytesMethods[syms.readFloat] = {bytesReadFloat, -1};
  syms.bytesMethods[syms.readDouble] = {bytesReadDouble, -1};
  syms.bytesMethods[syms.readString] = {bytesReadString, 2};

  syms.bytesMethods[syms.writeInt8] = {bytesWriteInt8, 2};
  syms.bytesMethods[syms.writeUInt8] = {bytesWriteUInt8, 2};
  syms.bytesMethods[syms.writeInt16] = {bytesWriteInt16, -1};
  syms.bytesMethods[syms.writeUInt16] = {bytesWriteUInt16, -1};
  syms.bytesMethods[syms.writeInt32] = {bytesWriteInt32, -1};
  syms.bytesMethods[syms.writeUInt32] = {bytesWriteUInt32, -1};

  syms.bytesMethods[syms.writeFloat] = {bytesWriteFloat, -1};
  syms.bytesMethods[syms.writeDouble] = {bytesWriteDouble, -1};
  syms.bytesMethods[syms.writeString] = {bytesWriteString, 2};

  syms.bytesMethods[syms.toStr] = {bytesToString, 0};
  syms.bytesMethods[syms.toHex] = {bytesToHex, 0};
}

void SptBytes::load(VM *vm) {
  SymbolTable &syms = const_cast<SymbolTable &>(vm->symbols());
  registerBytesMethods(syms);

  ClassObject *bytesClass = vm->allocateClass("Bytes");
  vm->protect(Value::object(bytesClass));

  addStaticMethod(vm, bytesClass, syms.create, bytesNew, 1);
  addStaticMethod(vm, bytesClass, syms.fromList, bytesFromList, 1);
  addStaticMethod(vm, bytesClass, syms.fromStr, bytesFromStr, 1);
  addStaticMethod(vm, bytesClass, syms.fromHex, bytesFromHex, 1);

  vm->defineGlobal("Bytes", Value::object(bytesClass));
  vm->unprotect(1);
}

bool getBytesProperty(VM *vm, Value object, StringObject *fieldName, Value &outValue) {
  const SymbolTable &syms = vm->symbols();
  auto *bytes = static_cast<BytesObject *>(object.asGC());

  if (fieldName == syms.length) {
    outValue = Value::integer(static_cast<int64_t>(bytes->data.size()));
    return true;
  }

  auto it = syms.bytesMethods.find(fieldName);
  if (it != syms.bytesMethods.end()) {
    outValue = createBytesBoundNative(vm, object, fieldName, it->second.fn, it->second.arity);
    return true;
  }

  return false;
}

bool invokeBytesMethod(VM *vm, Value receiver, StringObject *methodName, int argc, Value *argv,
                       Value &outResult) {
  const SymbolTable &syms = vm->symbols();

  auto it = syms.bytesMethods.find(methodName);
  if (it != syms.bytesMethods.end()) {
    outResult = it->second.fn(vm, receiver, argc, argv);
    return true;
  }

  return false;
}

} // namespace spt
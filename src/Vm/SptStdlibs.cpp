#include "SptStdlibs.h"
#include "Bytes.h"
#include "Fiber.h"
#include "Object.h"
#include "StringPool.h"
#include "VM.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace spt {

extern bool getBytesProperty(VM *vm, Value object, StringObject *fieldName, Value &outValue);
extern bool invokeBytesMethod(VM *vm, Value receiver, StringObject *methodName, int argc,
                              Value *argv, Value &outResult);

static int boundMethodDispatcher(VM *vm, Closure *self, int argc, Value *argv) {

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

static Value createBoundNative(VM *vm, Value receiver, StringObject *name,
                               BuiltinMethodDesc::MethodFn fn, int arity) {
  vm->protect(receiver);
  vm->protect(Value::object(name));

  Closure *native = vm->gc().allocateNativeClosure(1);
  native->name = name;
  native->arity = arity;
  native->receiver = receiver;

  native->function = boundMethodDispatcher;

  native->setNativeUpvalue(0, Value::integer(reinterpret_cast<int64_t>(fn)));

  vm->unprotect(2);
  return Value::object(native);
}

static Value listPush(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc >= 1) ? argv[0] : Value::nil();

  if (!receiver.isList())
    return Value::nil();
  auto *list = static_cast<ListObject *>(receiver.asGC());
  if (argc >= 1)
    list->elements.push_back(arg0);
  return Value::nil();
}

static Value listPop(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  auto *list = static_cast<ListObject *>(receiver.asGC());
  if (list->elements.empty())
    return Value::nil();
  Value result = list->elements.back();
  list->elements.pop_back();
  return result;
}

static Value listInsert(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();
  Value arg1 = (argc > 1) ? argv[1] : Value::nil();

  if (!receiver.isList() || argc < 2)
    return Value::nil();
  auto *list = static_cast<ListObject *>(receiver.asGC());
  if (!arg0.isInt())
    return Value::nil();
  int64_t idx = arg0.asInt();
  if (idx < 0)
    idx = 0;
  if (idx > static_cast<int64_t>(list->elements.size()))
    idx = list->elements.size();
  list->elements.insert(list->elements.begin() + idx, arg1);
  return Value::nil();
}

static Value listClear(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  static_cast<ListObject *>(receiver.asGC())->elements.clear();
  return Value::nil();
}

static Value listRemoveAt(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isList() || argc < 1)
    return Value::nil();
  auto *list = static_cast<ListObject *>(receiver.asGC());
  if (!arg0.isInt())
    return Value::nil();
  int64_t idx = arg0.asInt();
  if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size()))
    return Value::nil();
  Value result = list->elements[idx];
  list->elements.erase(list->elements.begin() + idx);
  return result;
}

static Value listSlice(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();
  Value arg1 = (argc > 1) ? argv[1] : Value::nil();

  if (!receiver.isList() || argc < 2)
    return Value::nil();

  if (!arg0.isInt() || !arg1.isInt()) {
    return Value::nil();
  }

  int64_t start = arg0.asInt();
  int64_t end = arg1.asInt();

  vm->protect(receiver);
  auto *list = static_cast<ListObject *>(receiver.asGC());

  int64_t len = list->elements.size();
  if (start < 0)
    start = std::max(int64_t(0), len + start);
  if (end < 0)
    end = std::max(int64_t(0), len + end);
  start = std::clamp(start, int64_t(0), len);
  end = std::clamp(end, int64_t(0), len);

  if (end <= start) {
    vm->unprotect(1);
    return Value::object(vm->allocateList(0));
  }

  auto *result = vm->allocateList(0);
  vm->protect(Value::object(result));
  list = static_cast<ListObject *>(receiver.asGC());
  for (int64_t i = start; i < end; ++i)
    result->elements.push_back(list->elements[i]);
  vm->unprotect(2);
  return Value::object(result);
}

static Value listJoin(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc >= 1) ? argv[0] : Value::nil();

  if (!receiver.isList())
    return Value::object(vm->allocateString(""));
  auto *list = static_cast<ListObject *>(receiver.asGC());
  std::string sep = (arg0.isString()) ? static_cast<StringObject *>(arg0.asGC())->str() : "";
  std::string result;
  for (size_t i = 0; i < list->elements.size(); ++i) {
    if (i > 0)
      result += sep;
    result += list->elements[i].toString();
  }
  return Value::object(vm->allocateString(result));
}

static Value listIndexOf(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isList() || argc < 1)
    return Value::integer(-1);
  auto *list = static_cast<ListObject *>(receiver.asGC());
  for (size_t i = 0; i < list->elements.size(); ++i)
    if (list->elements[i].equals(arg0))
      return Value::integer(i);
  return Value::integer(-1);
}

static Value listContains(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isList() || argc < 1)
    return Value::boolean(false);
  auto *list = static_cast<ListObject *>(receiver.asGC());
  for (const auto &elem : list->elements)
    if (elem.equals(arg0))
      return Value::boolean(true);
  return Value::boolean(false);
}

static Value mapHas(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isMap() || argc < 1)
    return Value::boolean(false);
  return Value::boolean(static_cast<MapObject *>(receiver.asGC())->has(arg0));
}

static Value mapClear(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  static_cast<MapObject *>(receiver.asGC())->entries.clear();
  return Value::nil();
}

static Value mapKeys(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  vm->protect(receiver);
  auto *result = vm->allocateList(0);
  vm->protect(Value::object(result));
  auto *map = static_cast<MapObject *>(receiver.asGC());
  for (const auto &entry : map->entries)
    result->elements.push_back(entry.first);
  vm->unprotect(2);
  return Value::object(result);
}

static Value mapValues(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  vm->protect(receiver);
  auto *map = static_cast<MapObject *>(receiver.asGC());
  auto *result = vm->allocateList(0);
  vm->protect(Value::object(result));
  map = static_cast<MapObject *>(receiver.asGC());
  for (const auto &entry : map->entries)
    result->elements.push_back(entry.second);
  vm->unprotect(2);
  return Value::object(result);
}

static Value mapRemove(VM *vm, Value receiver, int argc, Value *argv) {
  Value key = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isMap() || argc < 1)
    return Value::nil();
  auto *map = static_cast<MapObject *>(receiver.asGC());
  auto it = map->entries.find(key);
  if (it != map->entries.end()) {
    Value value = it->second;
    map->entries.erase(it);
    return value;
  }
  return Value::nil();
}

namespace Utf8Utils {

inline int sequenceLength(unsigned char c) {
  if ((c & 0x80) == 0)
    return 1;
  if ((c & 0xE0) == 0xC0)
    return 2;
  if ((c & 0xF0) == 0xE0)
    return 3;
  if ((c & 0xF8) == 0xF0)
    return 4;
  return 1;
}

int64_t countCharacters(const std::string &str) {
  int64_t count = 0;
  size_t i = 0;
  size_t len = str.length();
  const char *data = str.data();

  while (i < len) {
    count++;
    i += sequenceLength(static_cast<unsigned char>(data[i]));
  }
  return count;
}

size_t charIndexToByteOffset(const std::string &str, int64_t charIndex) {
  if (charIndex <= 0)
    return 0;

  size_t bytePos = 0;
  size_t len = str.length();
  int64_t currentStrIdx = 0;
  const char *data = str.data();

  while (bytePos < len && currentStrIdx < charIndex) {
    bytePos += sequenceLength(static_cast<unsigned char>(data[bytePos]));
    currentStrIdx++;
  }

  return bytePos;
}

int64_t byteOffsetToCharIndex(const std::string &str, size_t targetByteOffset) {
  int64_t charIdx = 0;
  size_t bytePos = 0;
  const char *data = str.data();

  while (bytePos < targetByteOffset && bytePos < str.length()) {
    bytePos += sequenceLength(static_cast<unsigned char>(data[bytePos]));
    charIdx++;
  }
  return charIdx;
}

void getSliceByteRange(const std::string &str, int64_t startChar, int64_t endChar,
                       size_t &outStartByte, size_t &outEndByte) {
  size_t bytePos = 0;
  int64_t currentChar = 0;
  size_t len = str.length();
  const char *data = str.data();

  outStartByte = len;
  outEndByte = len;

  while (bytePos < len) {
    if (currentChar == startChar)
      outStartByte = bytePos;
    if (currentChar == endChar) {
      outEndByte = bytePos;
      return;
    }

    bytePos += sequenceLength(static_cast<unsigned char>(data[bytePos]));
    currentChar++;
  }

  if (currentChar == startChar)
    outStartByte = bytePos;
  if (currentChar == endChar)
    outEndByte = bytePos;
}
} // namespace Utf8Utils

static Value stringLengthGetter(VM *vm, Value receiver) {
  if (!receiver.isString())
    return Value::integer(0);
  auto *str = static_cast<StringObject *>(receiver.asGC());

  return Value::integer(Utf8Utils::countCharacters(str->str()));
}

static Value stringByteLengthGetter(VM *vm, Value receiver) {
  if (!receiver.isString())
    return Value::integer(0);
  return Value::integer(static_cast<StringObject *>(receiver.asGC())->length);
}

static Value stringSlice(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();
  Value arg1 = (argc > 1) ? argv[1] : Value::nil();

  if (!receiver.isString())
    return Value::object(vm->allocateString(""));
  const std::string &strData = static_cast<StringObject *>(receiver.asGC())->str();

  if (!arg0.isInt())
    return receiver;

  int64_t charLen = Utf8Utils::countCharacters(strData);

  int64_t start = arg0.asInt();
  int64_t end = (argc > 1 && arg1.isInt()) ? arg1.asInt() : charLen;

  if (start < 0)
    start = charLen + start;
  if (end < 0)
    end = charLen + end;

  start = std::clamp(start, int64_t(0), charLen);
  end = std::clamp(end, int64_t(0), charLen);

  if (end <= start) {
    return Value::object(vm->allocateString(""));
  }

  size_t byteStart = 0;
  size_t byteEnd = 0;
  Utf8Utils::getSliceByteRange(strData, start, end, byteStart, byteEnd);

  return Value::object(vm->allocateString(strData.substr(byteStart, byteEnd - byteStart)));
}

static Value stringByteSlice(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();
  Value arg1 = (argc > 1) ? argv[1] : Value::nil();

  if (!receiver.isString())
    return Value::nil();
  const std::string &strData = static_cast<StringObject *>(receiver.asGC())->str();
  size_t byteLen = strData.length();

  int64_t start = (arg0.isInt()) ? arg0.asInt() : 0;
  int64_t end = (argc > 1 && arg1.isInt()) ? arg1.asInt() : byteLen;

  if (start < 0)
    start = byteLen + start;
  if (end < 0)
    end = byteLen + end;

  start = std::clamp(start, int64_t(0), (int64_t)byteLen);
  end = std::clamp(end, int64_t(0), (int64_t)byteLen);

  if (end <= start)
    return Value::object(vm->allocateString(""));

  return Value::object(vm->allocateString(strData.substr(start, end - start)));
}

static Value stringIndexOf(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isString() || !arg0.isString())
    return Value::integer(-1);

  const std::string &haystack = static_cast<StringObject *>(receiver.asGC())->str();
  const std::string &needle = static_cast<StringObject *>(arg0.asGC())->str();

  if (needle.empty())
    return Value::integer(0);

  size_t bytePos = haystack.find(needle);
  if (bytePos == std::string::npos) {
    return Value::integer(-1);
  }

  return Value::integer(Utf8Utils::byteOffsetToCharIndex(haystack, bytePos));
}

static Value stringSplit(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();
  if (!receiver.isString())
    return Value::nil();

  const std::string &strData = static_cast<StringObject *>(receiver.asGC())->str();
  std::string delim = (arg0.isString()) ? static_cast<StringObject *>(arg0.asGC())->str() : "";

  auto *list = vm->allocateList(0);
  vm->protect(Value::object(list));

  if (delim.empty()) {

    size_t i = 0;
    size_t len = strData.length();
    const char *data = strData.data();

    while (i < len) {
      int seqLen = Utf8Utils::sequenceLength(static_cast<unsigned char>(data[i]));

      list->elements.push_back(Value::object(vm->allocateString(strData.substr(i, seqLen))));
      i += seqLen;
    }
  } else {

    size_t start = 0;
    size_t end = 0;
    size_t delimLen = delim.length();

    while ((end = strData.find(delim, start)) != std::string::npos) {
      list->elements.push_back(
          Value::object(vm->allocateString(strData.substr(start, end - start))));
      start = end + delimLen;
    }

    list->elements.push_back(Value::object(vm->allocateString(strData.substr(start))));
  }

  vm->unprotect(1);
  return Value::object(list);
}

Value stringGetItem(VM *vm, Value receiver, int64_t index) {
  if (!receiver.isString())
    return Value::nil();
  const std::string &strData = static_cast<StringObject *>(receiver.asGC())->str();

  int64_t charLen = Utf8Utils::countCharacters(strData);
  if (index < 0)
    index = charLen + index;

  if (index < 0 || index >= charLen) {

    return Value::nil();
  }

  size_t byteStart = Utf8Utils::charIndexToByteOffset(strData, index);

  int seqLen = Utf8Utils::sequenceLength(static_cast<unsigned char>(strData[byteStart]));

  return Value::object(vm->allocateString(strData.substr(byteStart, seqLen)));
}

static Value stringContains(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isString() || argc < 1 || !arg0.isString())
    return Value::boolean(false);
  auto strView = static_cast<StringObject *>(receiver.asGC())->view();
  auto subView = static_cast<StringObject *>(arg0.asGC())->view();
  return Value::boolean(strView.find(subView) != std::string_view::npos);
}

static Value stringStartsWith(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isString() || argc < 1 || !arg0.isString())
    return Value::boolean(false);
  auto *str = static_cast<StringObject *>(receiver.asGC());
  auto *prefix = static_cast<StringObject *>(arg0.asGC());
  if (prefix->length > str->length)
    return Value::boolean(false);
  return Value::boolean(str->view().compare(0, prefix->length, prefix->view()) == 0);
}

static Value stringEndsWith(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isString() || argc < 1 || !arg0.isString())
    return Value::boolean(false);
  auto *str = static_cast<StringObject *>(receiver.asGC());
  auto *suffix = static_cast<StringObject *>(arg0.asGC());
  if (suffix->length > str->length)
    return Value::boolean(false);
  return Value::boolean(
      str->view().compare(str->length - suffix->length, suffix->length, suffix->view()) == 0);
}

static char toUpperChar(unsigned char c) { return static_cast<char>(std::toupper(c)); }

static char toLowerChar(unsigned char c) { return static_cast<char>(std::tolower(c)); }

static Value stringToUpper(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return receiver;
  std::string result = static_cast<StringObject *>(receiver.asGC())->str();
  std::transform(result.begin(), result.end(), result.begin(), toUpperChar);
  return Value::object(vm->allocateString(result));
}

static Value stringToLower(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return receiver;
  std::string result = static_cast<StringObject *>(receiver.asGC())->str();
  std::transform(result.begin(), result.end(), result.begin(), toLowerChar);
  return Value::object(vm->allocateString(result));
}

static Value stringTrim(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return receiver;
  std::string data = static_cast<StringObject *>(receiver.asGC())->str();
  size_t start = data.find_first_not_of(" \t\n\r\f\v");
  if (start == std::string::npos)
    return Value::object(vm->allocateString(""));
  size_t end = data.find_last_not_of(" \t\n\r\f\v");
  return Value::object(vm->allocateString(data.substr(start, end - start + 1)));
}

static Value stringReplace(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();
  Value arg1 = (argc > 1) ? argv[1] : Value::nil();

  if (!receiver.isString() || argc < 2 || !arg0.isString() || !arg1.isString())
    return receiver;
  auto *str = static_cast<StringObject *>(receiver.asGC());
  auto *oldStr = static_cast<StringObject *>(arg0.asGC());
  auto *newStr = static_cast<StringObject *>(arg1.asGC());
  if (oldStr->length == 0)
    return receiver;
  std::string result = str->str();
  std::string oldData = oldStr->str(), newData = newStr->str();
  size_t pos = 0;
  while ((pos = result.find(oldData, pos)) != std::string::npos) {
    result.replace(pos, oldData.length(), newData);
    pos += newData.length();
  }
  return Value::object(vm->allocateString(result));
}

static Value fiberCall(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isFiber()) {
    vm->throwError(Value::object(vm->allocateString("Expected fiber")));
    return Value::nil();
  }
  return vm->fiberCall(static_cast<FiberObject *>(receiver.asGC()), arg0, false);
}

static Value fiberTry(VM *vm, Value receiver, int argc, Value *argv) {
  Value arg0 = (argc > 0) ? argv[0] : Value::nil();

  if (!receiver.isFiber()) {
    vm->throwError(Value::object(vm->allocateString("Expected fiber")));
    return Value::nil();
  }
  auto *fiber = static_cast<FiberObject *>(receiver.asGC());
  Value result = vm->fiberCall(fiber, arg0, true);
  return fiber->hasError ? fiber->error : result;
}

void SymbolTable::registerBuiltinMethods() {

  listMethods[push] = {listPush, 1};
  listMethods[pop] = {listPop, 0};
  listMethods[insert] = {listInsert, 2};
  listMethods[clear] = {listClear, 0};
  listMethods[removeAt] = {listRemoveAt, 1};
  listMethods[indexOf] = {listIndexOf, 1};
  listMethods[contains] = {listContains, 1};
  listMethods[slice] = {listSlice, 2};
  listMethods[join] = {listJoin, -1};

  mapMethods[has] = {mapHas, 1};
  mapMethods[clear] = {mapClear, 0};
  mapMethods[keys] = {mapKeys, 0};
  mapMethods[values] = {mapValues, 0};
  mapMethods[remove] = {mapRemove, 1};

  stringMethods[slice] = {stringSlice, 2};
  stringMethods[byteSlice] = {stringByteSlice, 2};
  stringMethods[indexOf] = {stringIndexOf, 1};
  stringMethods[find] = {stringIndexOf, 1};
  stringMethods[contains] = {stringContains, 1};
  stringMethods[startsWith] = {stringStartsWith, 1};
  stringMethods[endsWith] = {stringEndsWith, 1};
  stringMethods[toUpper] = {stringToUpper, 0};
  stringMethods[toLower] = {stringToLower, 0};
  stringMethods[trim] = {stringTrim, 0};
  stringMethods[split] = {stringSplit, -1};
  stringMethods[replace] = {stringReplace, 2};

  fiberMethods[call] = {fiberCall, -1};
  fiberMethods[tryCall] = {fiberTry, -1};
}

bool StdlibDispatcher::getProperty(VM *vm, Value object, StringObject *fieldName, Value &outValue) {
  const SymbolTable &syms = vm->symbols();

  if (object.isList()) {
    auto *list = static_cast<ListObject *>(object.asGC());

    if (fieldName == syms.length) {
      outValue = Value::integer(list->elements.size());
      return true;
    }

    auto it = syms.listMethods.find(fieldName);
    if (it != syms.listMethods.end()) {
      outValue = createBoundNative(vm, object, fieldName, it->second.fn, it->second.arity);
      return true;
    }
    return false;
  }

  if (object.isMap()) {
    auto *map = static_cast<MapObject *>(object.asGC());
    if (fieldName == syms.size) {
      outValue = Value::integer(map->entries.size());
      return true;
    }
    auto it = syms.mapMethods.find(fieldName);
    if (it != syms.mapMethods.end()) {
      outValue = createBoundNative(vm, object, fieldName, it->second.fn, it->second.arity);
      return true;
    }
    return false;
  }

  if (object.isString()) {
    auto *str = static_cast<StringObject *>(object.asGC());
    if (fieldName == syms.length) {
      outValue = Value::integer(Utf8Utils::countCharacters(str->str()));
      return true;
    }
    if (fieldName == syms.byteLength) {
      outValue = Value::integer(str->length);
      return true;
    }
    auto it = syms.stringMethods.find(fieldName);
    if (it != syms.stringMethods.end()) {
      outValue = createBoundNative(vm, object, fieldName, it->second.fn, it->second.arity);
      return true;
    }
    return false;
  }

  if (object.isFiber()) {
    auto *fiber = static_cast<FiberObject *>(object.asGC());
    if (fieldName == syms.isDone) {
      outValue = Value::boolean(fiber->isDone() || fiber->isError());
      return true;
    }
    if (fieldName == syms.error) {
      outValue = fiber->hasError ? fiber->error : Value::nil();
      return true;
    }
    auto it = syms.fiberMethods.find(fieldName);
    if (it != syms.fiberMethods.end()) {
      outValue = createBoundNative(vm, object, fieldName, it->second.fn, it->second.arity);
      return true;
    }
    return false;
  }

  if (object.isBytes()) {
    return getBytesProperty(vm, object, fieldName, outValue);
  }

  if (object.isNativeInstance()) {
    auto *instance = static_cast<NativeInstance *>(object.asGC());
    auto it = instance->fields.find(fieldName);
    if (it != instance->fields.end()) {
      outValue = it->second;
      return true;
    }

    if (instance->klass) {
      if (Value *v = instance->klass->methods.get(fieldName)) {
        outValue = *v;
        return true;
      }
    }
    return false;
  }

  return false;
}

bool StdlibDispatcher::invokeMethod(VM *vm, Value receiver, StringObject *methodName, int argc,
                                    Value *argv, Value &outResult) {
  const SymbolTable &syms = vm->symbols();

  if (receiver.isList()) {
    auto it = syms.listMethods.find(methodName);
    if (it != syms.listMethods.end()) {
      outResult = it->second.fn(vm, receiver, argc, argv);
      return true;
    }
    return false;
  }

  if (receiver.isMap()) {
    auto it = syms.mapMethods.find(methodName);
    if (it != syms.mapMethods.end()) {
      outResult = it->second.fn(vm, receiver, argc, argv);
      return true;
    }
    return false;
  }

  if (receiver.isString()) {
    auto it = syms.stringMethods.find(methodName);
    if (it != syms.stringMethods.end()) {
      outResult = it->second.fn(vm, receiver, argc, argv);
      return true;
    }
    return false;
  }

  if (receiver.isFiber()) {
    auto it = syms.fiberMethods.find(methodName);
    if (it != syms.fiberMethods.end()) {
      outResult = it->second.fn(vm, receiver, argc, argv);
      return true;
    }
    return false;
  }

  if (receiver.isBytes()) {
    return invokeBytesMethod(vm, receiver, methodName, argc, argv, outResult);
  }

  if (receiver.isNativeInstance()) {
    auto *instance = static_cast<NativeInstance *>(receiver.asGC());

    if (instance->klass) {
      if (Value *v = instance->klass->methods.get(methodName)) {
        Value method = *v;

        if (method.isClosure()) {
          Closure *closure = static_cast<Closure *>(method.asGC());
          if (closure->isNative()) {
            closure->receiver = receiver;

            Value *returnSlot = vm->top();
            int nresults = closure->function(vm, closure, argc, argv);
            if (nresults > 0) {
              outResult = returnSlot[0];
            } else {
              outResult = Value::nil();
            }
            return true;
          }

          return false;
        }
      }
    }

    return false;
  }

  return false;
}

bool StdlibDispatcher::setProperty(VM *vm, Value object, StringObject *fieldName,
                                   const Value &value) {

  if (object.isNativeInstance()) {
    auto *instance = static_cast<NativeInstance *>(object.asGC());
    instance->setField(fieldName, value);
    return true;
  }

  return false;
}

bool StdlibDispatcher::getProperty(VM *vm, Value object, std::string_view fieldName,
                                   Value &outValue) {
  StringObject *key = vm->allocateString(fieldName);
  return getProperty(vm, object, key, outValue);
}

bool StdlibDispatcher::invokeMethod(VM *vm, Value receiver, std::string_view methodName, int argc,
                                    Value *argv, Value &outResult) {
  StringObject *key = vm->allocateString(methodName);
  return invokeMethod(vm, receiver, key, argc, argv, outResult);
}

bool StdlibDispatcher::setProperty(VM *vm, Value object, std::string_view fieldName,
                                   const Value &value) {
  StringObject *key = vm->allocateString(fieldName);
  return setProperty(vm, object, key, value);
}

} // namespace spt
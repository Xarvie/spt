#include "SptStdlibs.h"
#include "Fiber.h"
#include "NativeBinding.h"
#include "Object.h"
#include "VM.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace spt {

static Value createBoundNative(VM *vm, Value receiver, std::string_view name, NativeFn fn,
                               int arity) {
  NativeFunction *native = vm->gc().allocate<NativeFunction>();
  native->name = name;
  native->function = fn;
  native->arity = arity;
  native->receiver = receiver;
  return Value::object(native);
}

static Value createBoundNativeMethod(VM *vm, NativeInstance *instance, std::string_view name,
                                     const NativeMethodDesc &methodDesc) {

  NativeFunction *native = vm->gc().allocate<NativeFunction>();
  native->name = name;
  native->arity = methodDesc.arity;
  native->receiver = Value::object(instance);

  NativeMethodFn methodFn = methodDesc.function;
  native->function = [methodFn](VM *vm, Value receiver, int argc, Value *argv) -> Value {
    if (!receiver.isNativeInstance()) {
      vm->throwError(Value::object(vm->allocateString("Invalid receiver for native method")));
      return Value::nil();
    }
    NativeInstance *inst = static_cast<NativeInstance *>(receiver.asGC());
    if (!inst->isValid()) {
      vm->throwError(Value::object(vm->allocateString("Native instance has been destroyed")));
      return Value::nil();
    }
    return methodFn(vm, inst, argc, argv);
  };

  return Value::object(native);
}

static Value listPush(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  if (argc >= 1)
    list->elements.push_back(argv[0]);
  return Value::nil();
}

static Value listPop(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  if (list->elements.empty())
    return Value::nil();
  Value result = list->elements.back();
  list->elements.pop_back();
  return result;
}

static Value listInsert(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 2)
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  if (!argv[0].isInt())
    return Value::nil();
  int64_t idx = argv[0].asInt();
  if (idx < 0)
    idx = 0;
  if (idx > static_cast<int64_t>(list->elements.size()))
    idx = list->elements.size();
  list->elements.insert(list->elements.begin() + idx, argv[1]);
  return Value::nil();
}

static Value listClear(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  static_cast<ListObject *>(receiver.asGC())->elements.clear();
  return Value::nil();
}

static Value listRemoveAt(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 1)
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  if (!argv[0].isInt())
    return Value::nil();
  int64_t idx = argv[0].asInt();
  if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size()))
    return Value::nil();
  Value result = list->elements[idx];
  list->elements.erase(list->elements.begin() + idx);
  return result;
}

static Value listSlice(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 2)
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  if (!argv[0].isInt() || !argv[1].isInt())
    return Value::nil();
  int64_t start = argv[0].asInt();
  int64_t end = argv[1].asInt();
  int64_t len = list->elements.size();
  if (start < 0)
    start = std::max(int64_t(0), len + start);
  if (end < 0)
    end = std::max(int64_t(0), len + end);
  start = std::clamp(start, int64_t(0), len);
  end = std::clamp(end, int64_t(0), len);
  if (end <= start)
    return Value::object(vm->allocateList(0));
  ListObject *result = vm->allocateList(0);
  vm->protect(Value::object(result));
  for (int64_t i = start; i < end; ++i)
    result->elements.push_back(list->elements[i]);
  vm->unprotect(1);
  return Value::object(result);
}

static Value listJoin(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::object(vm->allocateString(""));
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  std::string sep = "";
  if (argc >= 1 && argv[0].isString())
    sep = static_cast<StringObject *>(argv[0].asGC())->str();
  std::string result;
  for (size_t i = 0; i < list->elements.size(); ++i) {
    if (i > 0)
      result += sep;
    result += list->elements[i].toString();
  }
  return Value::object(vm->allocateString(result));
}

static Value listIndexOf(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 1)
    return Value::integer(-1);
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  for (size_t i = 0; i < list->elements.size(); ++i) {
    if (list->elements[i].equals(argv[0]))
      return Value::integer(i);
  }
  return Value::integer(-1);
}

static Value listContains(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 1)
    return Value::boolean(false);
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  for (const auto &elem : list->elements) {
    if (elem.equals(argv[0]))
      return Value::boolean(true);
  }
  return Value::boolean(false);
}

static Value mapHas(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap() || argc < 1)
    return Value::boolean(false);
  return Value::boolean(static_cast<MapObject *>(receiver.asGC())->has(argv[0]));
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
  MapObject *map = static_cast<MapObject *>(receiver.asGC());
  ListObject *result = vm->allocateList(0);
  vm->protect(Value::object(result));
  for (const auto &entry : map->entries)
    result->elements.push_back(entry.first);
  vm->unprotect(1);
  return Value::object(result);
}

static Value mapValues(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  MapObject *map = static_cast<MapObject *>(receiver.asGC());
  ListObject *result = vm->allocateList(0);
  vm->protect(Value::object(result));
  for (const auto &entry : map->entries)
    result->elements.push_back(entry.second);
  vm->unprotect(1);
  return Value::object(result);
}

static Value mapRemove(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap() || argc < 1)
    return Value::nil();
  MapObject *map = static_cast<MapObject *>(receiver.asGC());
  Value key = argv[0];
  for (auto it = map->entries.begin(); it != map->entries.end(); ++it) {
    if (it->first.equals(key)) {
      Value value = it->second;
      map->entries.erase(it);
      return value;
    }
  }
  return Value::nil();
}

static Value stringSlice(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return Value::object(vm->allocateString(""));
  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  if (argc < 2 || !argv[0].isInt() || !argv[1].isInt())
    return Value::object(vm->allocateString(""));
  int64_t start = argv[0].asInt();
  int64_t end = argv[1].asInt();
  int64_t len = static_cast<int64_t>(str->length);
  if (start < 0)
    start = std::max(int64_t(0), len + start);
  if (end < 0)
    end = std::max(int64_t(0), len + end);
  start = std::clamp(start, int64_t(0), len);
  end = std::clamp(end, int64_t(0), len);
  if (end <= start)
    return Value::object(vm->allocateString(""));
  std::string_view sv = str->view();
  return Value::object(vm->allocateString(sv.substr(start, end - start)));
}

static Value stringIndexOf(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 1 || !argv[0].isString())
    return Value::integer(-1);
  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *sub = static_cast<StringObject *>(argv[0].asGC());
  std::string_view strView = str->view();
  std::string_view subView = sub->view();
  size_t pos = strView.find(subView);
  return Value::integer((pos == std::string_view::npos) ? -1 : static_cast<int64_t>(pos));
}

static Value stringContains(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 1 || !argv[0].isString())
    return Value::boolean(false);
  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *sub = static_cast<StringObject *>(argv[0].asGC());
  std::string_view strView = str->view();
  std::string_view subView = sub->view();
  return Value::boolean(strView.find(subView) != std::string_view::npos);
}

static Value stringStartsWith(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 1 || !argv[0].isString())
    return Value::boolean(false);
  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *prefix = static_cast<StringObject *>(argv[0].asGC());
  if (prefix->length > str->length)
    return Value::boolean(false);
  std::string_view strView = str->view();
  std::string_view prefixView = prefix->view();
  return Value::boolean(strView.compare(0, prefixView.size(), prefixView) == 0);
}

static Value stringEndsWith(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 1 || !argv[0].isString())
    return Value::boolean(false);
  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *suffix = static_cast<StringObject *>(argv[0].asGC());
  if (suffix->length > str->length)
    return Value::boolean(false);
  std::string_view strView = str->view();
  std::string_view suffixView = suffix->view();
  return Value::boolean(
      strView.compare(strView.size() - suffixView.size(), suffixView.size(), suffixView) == 0);
}

static Value stringToUpper(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return receiver;
  std::string result = static_cast<StringObject *>(receiver.asGC())->str();
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return Value::object(vm->allocateString(result));
}

static Value stringToLower(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return receiver;
  std::string result = static_cast<StringObject *>(receiver.asGC())->str();
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

static Value stringSplit(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return Value::nil();
  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  std::string delimiter =
      (argc >= 1 && argv[0].isString()) ? static_cast<StringObject *>(argv[0].asGC())->str() : "";
  ListObject *result = vm->allocateList(0);
  vm->protect(Value::object(result));

  std::string strData = str->str();
  if (delimiter.empty()) {
    for (char c : strData)
      result->elements.push_back(Value::object(vm->allocateString(std::string(1, c))));
  } else {
    size_t start = 0, end;
    while ((end = strData.find(delimiter, start)) != std::string::npos) {
      result->elements.push_back(
          Value::object(vm->allocateString(strData.substr(start, end - start))));
      start = end + delimiter.length();
    }
    result->elements.push_back(Value::object(vm->allocateString(strData.substr(start))));
  }
  vm->unprotect(1);
  return Value::object(result);
}

static Value stringFind(VM *vm, Value receiver, int argc, Value *argv) {
  return stringIndexOf(vm, receiver, argc, argv);
}

static Value stringReplace(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 2 || !argv[0].isString() || !argv[1].isString())
    return receiver;
  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *oldStr = static_cast<StringObject *>(argv[0].asGC());
  StringObject *newStr = static_cast<StringObject *>(argv[1].asGC());
  if (oldStr->length == 0)
    return receiver;
  std::string result = str->str();
  std::string oldData = oldStr->str();
  std::string newData = newStr->str();
  size_t pos = 0;
  while ((pos = result.find(oldData, pos)) != std::string::npos) {
    result.replace(pos, oldData.length(), newData);
    pos += newData.length();
  }
  return Value::object(vm->allocateString(result));
}

static const MethodEntry listMethods[] = {
    {"push", listPush, 1},         {"pop", listPop, 0},           {"insert", listInsert, 2},
    {"clear", listClear, 0},       {"removeAt", listRemoveAt, 1}, {"indexOf", listIndexOf, 1},
    {"contains", listContains, 1}, {"slice", listSlice, 2},       {"join", listJoin, -1},
    {nullptr, nullptr, 0}};

static const MethodEntry mapMethods[] = {{"has", mapHas, 1},       {"clear", mapClear, 0},
                                         {"keys", mapKeys, 0},     {"values", mapValues, 0},
                                         {"remove", mapRemove, 1}, {nullptr, nullptr, 0}};

static const MethodEntry stringMethods[] = {{"slice", stringSlice, 2},
                                            {"indexOf", stringIndexOf, 1},
                                            {"find", stringFind, 1},
                                            {"contains", stringContains, 1},
                                            {"startsWith", stringStartsWith, 1},
                                            {"endsWith", stringEndsWith, 1},
                                            {"toUpper", stringToUpper, 0},
                                            {"toLower", stringToLower, 0},
                                            {"trim", stringTrim, 0},
                                            {"split", stringSplit, -1},
                                            {"replace", stringReplace, 2},
                                            {nullptr, nullptr, 0}};

static Value fiberCall(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isFiber()) {
    vm->throwError(Value::object(vm->allocateString("Expected fiber")));
    return Value::nil();
  }

  return vm->fiberCall(static_cast<FiberObject *>(receiver.asGC()),
                       (argc > 0) ? argv[0] : Value::nil(), false);
}

static Value fiberTry(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isFiber()) {
    vm->throwError(Value::object(vm->allocateString("Expected fiber")));
    return Value::nil();
  }
  FiberObject *fiber = static_cast<FiberObject *>(receiver.asGC());

  Value result = vm->fiberCall(fiber, (argc > 0) ? argv[0] : Value::nil(), true);

  return fiber->hasError ? fiber->error : result;
}

static const MethodEntry fiberMethods[] = {
    {"call", fiberCall, -1}, {"try", fiberTry, -1}, {nullptr, nullptr, 0}};

static bool getNativeInstanceProperty(VM *vm, NativeInstance *instance, std::string_view fieldName,
                                      Value &outValue) {
  if (!instance || !instance->nativeClass)
    return false;

  const NativePropertyDesc *prop = instance->nativeClass->findProperty(fieldName);
  if (prop && prop->getter) {
    if (!instance->isValid()) {
      vm->throwError(Value::object(vm->allocateString("Native instance has been destroyed")));
      return false;
    }
    outValue = prop->getter(vm, instance);
    return true;
  }

  const NativeMethodDesc *method = instance->nativeClass->findMethod(fieldName);
  if (method) {

    vm->protect(Value::object(instance));
    outValue = createBoundNativeMethod(vm, instance, fieldName, *method);
    vm->unprotect(1);
    return true;
  }

  auto it = instance->fields.find(std::string_view(fieldName));
  if (it != instance->fields.end()) {
    outValue = it->second;
    return true;
  }

  return false;
}

static bool setNativeInstanceProperty(VM *vm, NativeInstance *instance, std::string_view fieldName,
                                      const Value &value) {
  if (!instance || !instance->nativeClass)
    return false;

  const NativePropertyDesc *prop = instance->nativeClass->findProperty(fieldName);
  if (prop) {
    if (prop->isReadOnly || !prop->setter) {
      std::string msg = "Cannot set read-only property: ";
      msg += fieldName;
      vm->throwError(Value::object(vm->allocateString(msg)));
      return false;
    }
    if (!instance->isValid()) {
      vm->throwError(Value::object(vm->allocateString("Native instance has been destroyed")));
      return false;
    }
    prop->setter(vm, instance, value);
    return true;
  }

  vm->protect(Value::object(instance));
  vm->protect(value);
  StringObject *nameStr = vm->allocateString(fieldName);
  instance->setField(nameStr, value);
  vm->unprotect(2);
  return true;
}

static bool invokeNativeInstanceMethod(VM *vm, NativeInstance *instance,
                                       std::string_view methodName, int argc, Value *argv,
                                       Value &outResult) {
  if (!instance || !instance->nativeClass)
    return false;

  const NativeMethodDesc *method = instance->nativeClass->findMethod(methodName);
  if (!method)
    return false;

  if (!instance->isValid()) {
    vm->throwError(Value::object(vm->allocateString("Native instance has been destroyed")));
    return false;
  }

  if (method->arity >= 0 && argc != method->arity) {
    std::string msg = "";
    msg += "Expected ";
    msg += " arguments but got " + std::to_string(argc);
    msg += " for method '";
    msg += methodName;
    msg += "'";
    vm->throwError(Value::object(vm->allocateString(msg)));
    return false;
  }

  outResult = method->function(vm, instance, argc, argv);
  return true;
}

static bool getNativeClassStatic(VM *vm, NativeClassObject *nativeClass, std::string_view name,
                                 Value &outValue) {
  if (!nativeClass)
    return false;

  auto it = nativeClass->statics.find(name);
  if (it != nativeClass->statics.end()) {
    outValue = it->second;
    return true;
  }

  if (nativeClass->baseClass) {
    return getNativeClassStatic(vm, nativeClass->baseClass, name, outValue);
  }

  return false;
}

bool StdlibDispatcher::getProperty(VM *vm, Value object, std::string_view fieldName,
                                   Value &outValue) {

  if (object.isList()) {
    ListObject *list = static_cast<ListObject *>(object.asGC());
    if (fieldName == "length") {
      outValue = Value::integer(list->elements.size());
      return true;
    }
    for (const MethodEntry *m = listMethods; m->name != nullptr; ++m) {
      if (fieldName == m->name) {
        outValue = createBoundNative(vm, object, m->name, m->fn, m->arity);
        return true;
      }
    }
    return false;
  }

  if (object.isMap()) {
    MapObject *map = static_cast<MapObject *>(object.asGC());
    if (fieldName == "size") {
      outValue = Value::integer(map->entries.size());
      return true;
    }
    for (const MethodEntry *m = mapMethods; m->name != nullptr; ++m) {
      if (fieldName == m->name) {
        outValue = createBoundNative(vm, object, m->name, m->fn, m->arity);
        return true;
      }
    }
    return false;
  }

  if (object.isString()) {
    StringObject *str = static_cast<StringObject *>(object.asGC());
    if (fieldName == "length") {
      outValue = Value::integer(str->length);
      return true;
    }
    for (const MethodEntry *m = stringMethods; m->name != nullptr; ++m) {
      if (fieldName == m->name) {
        outValue = createBoundNative(vm, object, m->name, m->fn, m->arity);
        return true;
      }
    }
    return false;
  }

  if (object.isFiber()) {
    FiberObject *fiber = static_cast<FiberObject *>(object.asGC());

    if (fieldName == "isDone") {
      outValue = Value::boolean(fiber->isDone() || fiber->isError());
      return true;
    }
    if (fieldName == "error") {
      outValue = fiber->hasError ? fiber->error : Value::nil();
      return true;
    }

    for (const MethodEntry *m = fiberMethods; m->name != nullptr; ++m) {
      if (fieldName == m->name) {
        outValue = createBoundNative(vm, object, m->name, m->fn, m->arity);
        return true;
      }
    }
    return false;
  }

  if (object.isNativeInstance()) {
    NativeInstance *instance = static_cast<NativeInstance *>(object.asGC());
    return getNativeInstanceProperty(vm, instance, fieldName, outValue);
  }

  if (object.isNativeClass()) {
    NativeClassObject *nativeClass = static_cast<NativeClassObject *>(object.asGC());
    return getNativeClassStatic(vm, nativeClass, fieldName, outValue);
  }

  return false;
}

bool StdlibDispatcher::invokeMethod(VM *vm, Value receiver, std::string_view methodName, int argc,
                                    Value *argv, Value &outResult) {
  if (receiver.isList()) {
    for (const MethodEntry *m = listMethods; m->name != nullptr; ++m) {
      if (methodName == m->name) {
        outResult = m->fn(vm, receiver, argc, argv);
        return true;
      }
    }
    return false;
  }
  if (receiver.isMap()) {
    for (const MethodEntry *m = mapMethods; m->name != nullptr; ++m) {
      if (methodName == m->name) {
        outResult = m->fn(vm, receiver, argc, argv);
        return true;
      }
    }
    return false;
  }
  if (receiver.isString()) {
    for (const MethodEntry *m = stringMethods; m->name != nullptr; ++m) {
      if (methodName == m->name) {
        outResult = m->fn(vm, receiver, argc, argv);
        return true;
      }
    }
    return false;
  }
  if (receiver.isFiber()) {
    for (const MethodEntry *m = fiberMethods; m->name != nullptr; ++m) {
      if (methodName == m->name) {
        outResult = m->fn(vm, receiver, argc, argv);
        return true;
      }
    }
    return false;
  }

  if (receiver.isNativeInstance()) {
    NativeInstance *instance = static_cast<NativeInstance *>(receiver.asGC());
    return invokeNativeInstanceMethod(vm, instance, methodName, argc, argv, outResult);
  }

  return false;
}

bool StdlibDispatcher::setProperty(VM *vm, Value object, std::string_view fieldName,
                                   const Value &value) {

  if (object.isNativeInstance()) {
    NativeInstance *instance = static_cast<NativeInstance *>(object.asGC());
    return setNativeInstanceProperty(vm, instance, fieldName, value);
  }

  return false;
}

} // namespace spt
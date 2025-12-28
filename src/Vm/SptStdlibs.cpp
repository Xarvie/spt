#include "SptStdlibs.h"
#include "Object.h"
#include "VM.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace spt {

static Value createBoundNative(VM *vm, Value receiver, const std::string &name, NativeFn fn,
                               int arity) {
  NativeFunction *native = vm->gc().allocate<NativeFunction>();
  native->name = name;
  native->function = fn;
  native->arity = arity;
  native->receiver = receiver;
  return Value::object(native);
}

static Value listPush(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());

  if (argc >= 1) {
    list->elements.push_back(argv[0]);
  }
  return Value::nil();
}

static Value listPop(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());

  if (list->elements.empty()) {
    return Value::nil();
  }

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
  if (idx > static_cast<int64_t>(list->elements.size())) {
    idx = static_cast<int64_t>(list->elements.size());
  }

  list->elements.insert(list->elements.begin() + idx, argv[1]);
  return Value::nil();
}

static Value listClear(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  list->elements.clear();
  return Value::nil();
}

static Value listRemoveAt(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 1)
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());

  if (!argv[0].isInt())
    return Value::nil();
  int64_t idx = argv[0].asInt();

  if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size())) {
    return Value::nil();
  }

  Value result = list->elements[idx];
  list->elements.erase(list->elements.begin() + idx);
  return result;
}

static Value listIndexOf(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 1)
    return Value::integer(-1);
  ListObject *list = static_cast<ListObject *>(receiver.asGC());

  for (size_t i = 0; i < list->elements.size(); ++i) {
    if (list->elements[i].equals(argv[0])) {
      return Value::integer(static_cast<int64_t>(i));
    }
  }
  return Value::integer(-1);
}

static Value listContains(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 1)
    return Value::boolean(false);
  ListObject *list = static_cast<ListObject *>(receiver.asGC());

  for (const auto &elem : list->elements) {
    if (elem.equals(argv[0])) {
      return Value::boolean(true);
    }
  }
  return Value::boolean(false);
}

static Value mapHas(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap() || argc < 1)
    return Value::boolean(false);
  MapObject *map = static_cast<MapObject *>(receiver.asGC());
  return Value::boolean(map->has(argv[0]));
}

static Value mapClear(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  MapObject *map = static_cast<MapObject *>(receiver.asGC());
  map->entries.clear();
  return Value::nil();
}

static Value mapKeys(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  MapObject *map = static_cast<MapObject *>(receiver.asGC());

  ListObject *result = vm->allocateList(0);
  vm->protect(Value::object(result));

  for (const auto &entry : map->entries) {
    result->elements.push_back(entry.first);
  }

  vm->unprotect(1);
  return Value::object(result);
}

static Value mapValues(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  MapObject *map = static_cast<MapObject *>(receiver.asGC());

  ListObject *result = vm->allocateList(0);
  vm->protect(Value::object(result));

  for (const auto &entry : map->entries) {
    result->elements.push_back(entry.second);
  }

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
  const std::string &data = str->data;

  if (argc < 2 || !argv[0].isInt() || !argv[1].isInt()) {
    return Value::object(vm->allocateString(""));
  }

  int64_t start = argv[0].asInt();
  int64_t end = argv[1].asInt();
  int64_t len = static_cast<int64_t>(data.size());

  if (start < 0)
    start = std::max(int64_t(0), len + start);
  if (end < 0)
    end = std::max(int64_t(0), len + end);

  start = std::clamp(start, int64_t(0), len);
  end = std::clamp(end, int64_t(0), len);

  if (end <= start) {
    return Value::object(vm->allocateString(""));
  }

  std::string result = data.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
  return Value::object(vm->allocateString(result));
}

static Value stringIndexOf(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 1)
    return Value::integer(-1);
  if (!argv[0].isString())
    return Value::integer(-1);

  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *substr = static_cast<StringObject *>(argv[0].asGC());

  size_t pos = str->data.find(substr->data);
  if (pos == std::string::npos) {
    return Value::integer(-1);
  }
  return Value::integer(static_cast<int64_t>(pos));
}

static Value stringContains(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 1)
    return Value::boolean(false);
  if (!argv[0].isString())
    return Value::boolean(false);

  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *substr = static_cast<StringObject *>(argv[0].asGC());

  return Value::boolean(str->data.find(substr->data) != std::string::npos);
}

static Value stringStartsWith(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 1)
    return Value::boolean(false);
  if (!argv[0].isString())
    return Value::boolean(false);

  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *prefix = static_cast<StringObject *>(argv[0].asGC());

  if (prefix->data.size() > str->data.size())
    return Value::boolean(false);

  return Value::boolean(str->data.compare(0, prefix->data.size(), prefix->data) == 0);
}

static Value stringEndsWith(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString() || argc < 1)
    return Value::boolean(false);
  if (!argv[0].isString())
    return Value::boolean(false);

  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  StringObject *suffix = static_cast<StringObject *>(argv[0].asGC());

  if (suffix->data.size() > str->data.size())
    return Value::boolean(false);

  return Value::boolean(str->data.compare(str->data.size() - suffix->data.size(),
                                          suffix->data.size(), suffix->data) == 0);
}

static Value stringToUpper(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return receiver;

  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  std::string result = str->data;
  std::transform(result.begin(), result.end(), result.begin(), ::toupper);
  return Value::object(vm->allocateString(result));
}

static Value stringToLower(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return receiver;

  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  std::string result = str->data;
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return Value::object(vm->allocateString(result));
}

static Value stringTrim(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return receiver;

  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  const std::string &data = str->data;

  size_t start = data.find_first_not_of(" \t\n\r\f\v");
  if (start == std::string::npos) {
    return Value::object(vm->allocateString(""));
  }
  size_t end = data.find_last_not_of(" \t\n\r\f\v");

  return Value::object(vm->allocateString(data.substr(start, end - start + 1)));
}

static Value stringSplit(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isString())
    return Value::nil();

  StringObject *str = static_cast<StringObject *>(receiver.asGC());
  std::string delimiter = "";

  if (argc >= 1 && argv[0].isString()) {
    delimiter = static_cast<StringObject *>(argv[0].asGC())->data;
  }

  ListObject *result = vm->allocateList(0);
  vm->protect(Value::object(result));

  if (delimiter.empty()) {

    for (char c : str->data) {
      result->elements.push_back(Value::object(vm->allocateString(std::string(1, c))));
    }
  } else {
    size_t start = 0;
    size_t end;
    while ((end = str->data.find(delimiter, start)) != std::string::npos) {
      result->elements.push_back(
          Value::object(vm->allocateString(str->data.substr(start, end - start))));
      start = end + delimiter.length();
    }
    result->elements.push_back(Value::object(vm->allocateString(str->data.substr(start))));
  }

  vm->unprotect(1);
  return Value::object(result);
}

struct MethodEntry {
  const char *name;
  NativeFn fn;
  int arity;
};

static const MethodEntry listMethods[] = {
    {"push", listPush, 1},         {"pop", listPop, 0},           {"insert", listInsert, 2},
    {"clear", listClear, 0},       {"removeAt", listRemoveAt, 1}, {"indexOf", listIndexOf, 1},
    {"contains", listContains, 1}, {nullptr, nullptr, 0}};

static const MethodEntry mapMethods[] = {{"has", mapHas, 1},       {"clear", mapClear, 0},
                                         {"keys", mapKeys, 0},     {"values", mapValues, 0},
                                         {"remove", mapRemove, 1}, {nullptr, nullptr, 0}};

static const MethodEntry stringMethods[] = {
    {"slice", stringSlice, 2},       {"indexOf", stringIndexOf, 1},
    {"contains", stringContains, 1}, {"startsWith", stringStartsWith, 1},
    {"endsWith", stringEndsWith, 1}, {"toUpper", stringToUpper, 0},
    {"toLower", stringToLower, 0},   {"trim", stringTrim, 0},
    {"split", stringSplit, -1},      {nullptr, nullptr, 0}};

bool StdlibDispatcher::getProperty(VM *vm, Value object, const std::string &fieldName,
                                   Value &outValue) {

  if (object.isList()) {
    ListObject *list = static_cast<ListObject *>(object.asGC());

    if (fieldName == "length") {
      outValue = Value::integer(static_cast<int64_t>(list->elements.size()));
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
      outValue = Value::integer(static_cast<int64_t>(map->entries.size()));
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
      outValue = Value::integer(static_cast<int64_t>(str->data.size()));
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

  return false;
}

bool StdlibDispatcher::invokeMethod(VM *vm, Value receiver, const std::string &methodName, int argc,
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

  return false;
}

} // namespace spt
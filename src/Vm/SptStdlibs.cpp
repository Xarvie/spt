#include "SptStdlibs.h"
#include "Object.h"
#include "VM.h"
#include <algorithm>

namespace spt {

// ============================================================================
// 🛠️ Helper: Create Bound Native Function
// ============================================================================

/**
 * Creates a NativeFunction object and binds 'receiver' to it.
 * This effectively creates a "method" (closure) bound to 'this'.
 */
static Value createBoundNative(VM *vm, Value receiver, const std::string &name, NativeFn fn,
                               int arity) {
  // 使用 VM 的 GC 分配器
  NativeFunction *native = vm->gc().allocate<NativeFunction>();
  native->name = name;
  native->function = fn;
  native->arity = arity;
  native->receiver = receiver; // <--- 关键：绑定 this
  return Value::object(native);
}

// ============================================================================
// 📜 List Methods
// ============================================================================

// List.push(val)
static Value listPush(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());

  if (argc >= 1) {
    list->elements.push_back(argv[0]);
  }
  return Value::nil();
}

// List.pop()
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

// List.insert(idx, val)
static Value listInsert(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 2)
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());

  if (!argv[0].isInt())
    return Value::nil(); // Error handling could be better
  int64_t idx = argv[0].asInt();

  if (idx < 0)
    idx = 0;
  if (idx > static_cast<int64_t>(list->elements.size())) {
    idx = static_cast<int64_t>(list->elements.size());
  }

  list->elements.insert(list->elements.begin() + idx, argv[1]);
  return Value::nil();
}

// List.clear() -> void (Capacity preserved)
static Value listClear(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList())
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());
  list->elements.clear();
  return Value::nil();
}

// ============================================================================
// [新增] List.removeAt(index) -> value
// ============================================================================
static Value listRemoveAt(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isList() || argc < 1)
    return Value::nil();
  ListObject *list = static_cast<ListObject *>(receiver.asGC());

  if (!argv[0].isInt())
    return Value::nil();
  int64_t idx = argv[0].asInt();

  // 边界检查
  if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size())) {
    return Value::nil();
  }

  Value result = list->elements[idx];
  // 移除元素，后面的元素自动前移
  list->elements.erase(list->elements.begin() + idx);
  return result;
}

// ============================================================================
// 🗺️ Map Methods
// ============================================================================

// Map.has(key) -> bool
static Value mapHas(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap() || argc < 1)
    return Value::boolean(false);
  MapObject *map = static_cast<MapObject *>(receiver.asGC());
  return Value::boolean(map->has(argv[0]));
}

// Map.clear() -> void
static Value mapClear(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  MapObject *map = static_cast<MapObject *>(receiver.asGC());
  map->entries.clear();
  return Value::nil();
}

// Map.keys() -> List
static Value mapKeys(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap())
    return Value::nil();
  MapObject *map = static_cast<MapObject *>(receiver.asGC());

  // 1. Allocate new list
  ListObject *result = vm->allocateList(static_cast<int>(map->entries.size()));
  vm->protect(Value::object(result)); // GC Protection

  result->elements.clear(); // allocateList might fill with nils

  // 2. Populate
  for (const auto &entry : map->entries) {
    result->elements.push_back(entry.first);
  }

  vm->unprotect(1);
  return Value::object(result);
}

// ============================================================================
// [新增] Map.remove(key) -> value
// ============================================================================
static Value mapRemove(VM *vm, Value receiver, int argc, Value *argv) {
  if (!receiver.isMap() || argc < 1)
    return Value::nil();
  MapObject *map = static_cast<MapObject *>(receiver.asGC());

  Value key = argv[0];

  // 遍历查找 key
  for (auto it = map->entries.begin(); it != map->entries.end(); ++it) {
    if (it->first.equals(key)) {
      Value value = it->second;

      // 移除条目
      // 注意：使用 erase 可以保持剩余元素的插入顺序 (虽然是 O(N))
      // 如果为了极致性能且不关心顺序，可以用: *it = map->entries.back(); map->entries.pop_back();
      map->entries.erase(it);

      return value;
    }
  }
  return Value::nil();
}

// ============================================================================
// 🧵 String Methods
// ============================================================================

// String.slice(start, end) -> String
// Spec: [start, end) byte-based slicing
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
  size_t len = data.size();

  // Clamp start
  if (start < 0)
    start = 0;
  if (start > static_cast<int64_t>(len))
    start = static_cast<int64_t>(len);

  // Clamp end
  if (end < 0)
    end = 0;
  if (end > static_cast<int64_t>(len))
    end = static_cast<int64_t>(len);

  // Calculate length
  int64_t sliceLen = end - start;
  if (sliceLen <= 0) {
    return Value::object(vm->allocateString(""));
  }

  std::string result = data.substr(static_cast<size_t>(start), static_cast<size_t>(sliceLen));
  return Value::object(vm->allocateString(result));
}

// ============================================================================
// 🚦 Main Dispatcher
// ============================================================================

bool StdlibDispatcher::getProperty(VM *vm, Value object, const std::string &fieldName,
                                   Value &outValue) {

  // ------------------------------------------------------------------------
  // 1. List
  // ------------------------------------------------------------------------
  if (object.isList()) {
    ListObject *list = static_cast<ListObject *>(object.asGC());

    // Properties
    if (fieldName == "length") {
      outValue = Value::integer(static_cast<int64_t>(list->elements.size()));
      return true;
    }

    // Methods (Bound NativeFunctions)
    if (fieldName == "push") {
      outValue = createBoundNative(vm, object, "push", listPush, 1);
      return true;
    }
    if (fieldName == "pop") {
      outValue = createBoundNative(vm, object, "pop", listPop, 0);
      return true;
    }
    if (fieldName == "insert") {
      outValue = createBoundNative(vm, object, "insert", listInsert, 2);
      return true;
    }
    if (fieldName == "clear") {
      outValue = createBoundNative(vm, object, "clear", listClear, 0);
      return true;
    }
    if (fieldName == "removeAt") {
      outValue = createBoundNative(vm, object, "removeAt", listRemoveAt, 1);
      return true;
    }
    return false;
  }

  // ------------------------------------------------------------------------
  // 2. Map
  // ------------------------------------------------------------------------
  if (object.isMap()) {
    MapObject *map = static_cast<MapObject *>(object.asGC());

    // Properties
    if (fieldName == "size") {
      outValue = Value::integer(static_cast<int64_t>(map->entries.size()));
      return true;
    }

    // Methods
    if (fieldName == "has") {
      outValue = createBoundNative(vm, object, "has", mapHas, 1);
      return true;
    }
    if (fieldName == "clear") {
      outValue = createBoundNative(vm, object, "clear", mapClear, 0);
      return true;
    }
    if (fieldName == "keys") {
      outValue = createBoundNative(vm, object, "keys", mapKeys, 0);
      return true;
    }
    if (fieldName == "remove") {
      outValue = createBoundNative(vm, object, "remove", mapRemove, 1);
      return true;
    }
    return false;
  }

  // ------------------------------------------------------------------------
  // 3. String
  // ------------------------------------------------------------------------
  if (object.isString()) {
    StringObject *str = static_cast<StringObject *>(object.asGC());

    // Properties
    if (fieldName == "length") {
      outValue = Value::integer(static_cast<int64_t>(str->data.size()));
      return true;
    }

    // Methods
    if (fieldName == "slice") {
      outValue = createBoundNative(vm, object, "slice", stringSlice, 2);
      return true;
    }
    return false;
  }

  return false;
}

} // namespace spt
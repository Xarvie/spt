#include "Value.h"
#include "NativeBinding.h"
#include "Object.h"

namespace spt {

std::string Value::toString() const {
  switch (type) {
  case ValueType::Nil:
    return "nil";
  case ValueType::Bool:
    return as.boolean ? "true" : "false";
  case ValueType::Int:
    return std::to_string(as.integer);
  case ValueType::Float:
    return std::to_string(as.number);
  case ValueType::String:
    return as.gc ? static_cast<StringObject *>(as.gc)->data : "";
  case ValueType::List:
    return "<list>";
  case ValueType::Map:
    return "<map>";
  case ValueType::Object:
    return "<instance>";
  case ValueType::Closure:
    return "<function>";
  case ValueType::Class:
    return "<class>";
  case ValueType::NativeFunc:
    return "<native function>";
  case ValueType::Fiber:
    return "<fiber>";
  case ValueType::NativeClass: {
    NativeClassObject *nc = static_cast<NativeClassObject *>(as.gc);
    return "<native class " + (nc ? nc->name : "?") + ">";
  }
  case ValueType::NativeObject: {
    NativeInstance *ni = static_cast<NativeInstance *>(as.gc);
    if (ni && ni->nativeClass) {
      return "<" + ni->nativeClass->name + " instance>";
    }
    return "<native instance>";
  }
  default:
    return "<unknown>";
  }
}

const char *Value::typeName() const {
  switch (type) {
  case ValueType::Nil:
    return "nil";
  case ValueType::Bool:
    return "bool";
  case ValueType::Int:
    return "int";
  case ValueType::Float:
    return "float";
  case ValueType::String:
    return "string";
  case ValueType::List:
    return "list";
  case ValueType::Map:
    return "map";
  case ValueType::Object:
    return "instance";
  case ValueType::Closure:
    return "function";
  case ValueType::Class:
    return "class";
  case ValueType::NativeFunc:
    return "native";
  case ValueType::Fiber:
    return "fiber";
  case ValueType::NativeClass:
    return "native_class";
  case ValueType::NativeObject:
    return "native_instance";
  default:
    return "unknown";
  }
}

bool Value::equals(const Value &other) const {
  if (type != other.type)
    return false;

  switch (type) {
  case ValueType::Nil:
    return true;
  case ValueType::Bool:
    return as.boolean == other.as.boolean;
  case ValueType::Int:
    return as.integer == other.as.integer;
  case ValueType::Float:
    return as.number == other.as.number;
  case ValueType::String:
    if (as.gc && other.as.gc) {
      return static_cast<StringObject *>(as.gc)->data ==
             static_cast<StringObject *>(other.as.gc)->data;
    }
    return as.gc == other.as.gc;
  default:
    return as.gc == other.as.gc;
  }
}

size_t Value::hash() const noexcept {
  switch (type) {
  case ValueType::Nil:
    return 0;
  case ValueType::Bool:
    return as.boolean ? 1 : 0;
  case ValueType::Int:
    return as.integer;
  case ValueType::Float:
    return std::hash<double>()(as.number);
  case ValueType::String:
    if (as.gc) {
      return std::hash<std::string>()(static_cast<StringObject *>(as.gc)->data);
    }
    return 0;
  default:
    return std::hash<void *>()(as.gc);
  }
}

Value MapObject::get(const Value &key) const {
  auto it = entries.find(key);
  if (it == entries.end())
    return Value::nil();
  return it->second;
}

void MapObject::set(const Value &key, const Value &value) { entries[key] = value; }

bool MapObject::has(const Value &key) const { return entries.find(key) != entries.end(); }

} // namespace spt

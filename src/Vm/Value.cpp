#include "Value.h"
#include "Object.h"
#include <cmath>
#include <cstdio>

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
    if (as.gc) {
      return static_cast<StringObject *>(as.gc)->str();
    }
    return "";
  case ValueType::List:
    return "<list>";
  case ValueType::Map:
    return "<map>";
  case ValueType::Object:
    return "<instance>";
  case ValueType::Closure: {
    Closure *closure = static_cast<Closure *>(as.gc);
    if (closure->isNative()) {
      return "<native function>";
    }
    return "<function>";
  }
  case ValueType::Class:
    return "<class>";
  case ValueType::Fiber:
    return "<fiber>";
  case ValueType::NativeObject: {
    NativeInstance *ni = static_cast<NativeInstance *>(as.gc);
    if (ni && ni->klass) {
      return "<" + ni->klass->name + " instance>";
    }
    return "<native instance>";
  }
  case ValueType::LightUserData: {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "<lightuserdata: %p>", as.lightUserData);
    return buf;
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
  case ValueType::Closure: {
    Closure *closure = static_cast<Closure *>(as.gc);
    if (closure && closure->isNative()) {
      return "native";
    }
    return "function";
  }
  case ValueType::Class:
    return "class";
  case ValueType::Fiber:
    return "fiber";
  case ValueType::NativeObject:
    return "native_instance";
  case ValueType::LightUserData:
    return "lightuserdata";
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
    if (std::isnan(as.number) || std::isnan(other.as.number)) {
      return false;
    }
    return as.number == other.as.number;
  case ValueType::String:

    return as.gc == other.as.gc;
  case ValueType::LightUserData:
    return as.lightUserData == other.as.lightUserData;
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
    return static_cast<size_t>(as.integer);
  case ValueType::Float:
    if (std::isnan(as.number)) {
      return 0x7FF8000000000001ULL;
    }
    return std::hash<double>()(as.number);
  case ValueType::String:

    if (as.gc) {
      return static_cast<StringObject *>(as.gc)->hash;
    }
    return 0;
  case ValueType::LightUserData:
    return std::hash<void *>()(as.lightUserData);
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
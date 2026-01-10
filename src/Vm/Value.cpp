#include "Value.h"
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

Value MapObject::get(const Value &key) const {
  for (const auto &[k, v] : entries) {
    if (k.equals(key))
      return v;
  }
  return Value::nil();
}

void MapObject::set(const Value &key, const Value &value) {
  for (auto &[k, v] : entries) {
    if (k.equals(key)) {
      v = value;
      return;
    }
  }
  entries.emplace_back(key, value);
}

bool MapObject::has(const Value &key) const {
  for (const auto &[k, v] : entries) {
    if (k.equals(key))
      return true;
  }
  return false;
}

} // namespace spt

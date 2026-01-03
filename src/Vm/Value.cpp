#include "Value.h"
#include "Object.h"

namespace spt {

Value Value::nil() {
  Value v;
  v.type = ValueType::Nil;
  v.as.gc = nullptr;
  return v;
}

Value Value::boolean(bool b) {
  Value v;
  v.type = ValueType::Bool;
  v.as.boolean = b;
  return v;
}

Value Value::integer(int64_t i) {
  Value v;
  v.type = ValueType::Int;
  v.as.integer = i;
  return v;
}

Value Value::number(double n) {
  Value v;
  v.type = ValueType::Float;
  v.as.number = n;
  return v;
}

Value Value::object(GCObject *obj) {
  Value v;
  v.type = obj ? obj->type : ValueType::Nil;
  v.as.gc = obj;
  return v;
}

bool Value::isNil() const { return type == ValueType::Nil; }

bool Value::isBool() const { return type == ValueType::Bool; }

bool Value::isInt() const { return type == ValueType::Int; }

bool Value::isFloat() const { return type == ValueType::Float; }

bool Value::isNumber() const { return type == ValueType::Float || type == ValueType::Int; }

bool Value::isString() const { return type == ValueType::String; }

bool Value::isList() const { return type == ValueType::List; }

bool Value::isMap() const { return type == ValueType::Map; }

bool Value::isInstance() const { return type == ValueType::Object; }

bool Value::isClosure() const { return type == ValueType::Closure; }

bool Value::isClass() const { return type == ValueType::Class; }

bool Value::isNativeFunc() const { return type == ValueType::NativeFunc; }

bool Value::isFiber() const { return type == ValueType::Fiber; }

bool Value::asBool() const { return as.boolean; }

int64_t Value::asInt() const { return as.integer; }

double Value::asFloat() const { return as.number; }

double Value::asNumber() const {
  return type == ValueType::Int ? static_cast<double>(as.integer) : as.number;
}

GCObject *Value::asGC() const { return as.gc; }

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

bool Value::toBool() const {
  if (type == ValueType::Nil)
    return false;
  if (type == ValueType::Bool)
    return as.boolean;
  return true;
}

bool Value::isTruthy() const {

  if (type == ValueType::Nil)
    return false;
  if (type == ValueType::Bool)
    return as.boolean;
  return true;
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

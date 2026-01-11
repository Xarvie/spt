#pragma once
#include "Vm/NativeBinding.h"
#include <cmath>
#include <string>

// ============================================================================
// Native Class Definitions - 原生类定义
// ============================================================================

// ----------------------------------------------------------------------------
// Vector3 类 - 演示属性、方法、静态方法
// ----------------------------------------------------------------------------
class Vector3 {
public:
  double x, y, z;

  Vector3() : x(0), y(0), z(0) {}

  Vector3(double x, double y, double z) : x(x), y(y), z(z) {}

  double length() const { return std::sqrt(x * x + y * y + z * z); }

  void normalize() {
    double len = length();
    if (len > 0) {
      x /= len;
      y /= len;
      z /= len;
    }
  }

  Vector3 add(const Vector3 &other) const { return Vector3(x + other.x, y + other.y, z + other.z); }

  double dot(const Vector3 &other) const { return x * other.x + y * other.y + z * other.z; }

  Vector3 scale(double s) const { return Vector3(x * s, y * s, z * s); }
};

// Vector3 注册函数
inline void registerVector3(VM *vm) {
  NativeClassBuilder<Vector3>(vm, "Vector3")
      // 构造函数
      .constructor([](VM *vm, int argc, Value *argv) -> void * {
        double x = (argc > 0 && argv[0].isNumber()) ? argv[0].asNumber() : 0.0;
        double y = (argc > 1 && argv[1].isNumber()) ? argv[1].asNumber() : 0.0;
        double z = (argc > 2 && argv[2].isNumber()) ? argv[2].asNumber() : 0.0;
        return new Vector3(x, y, z);
      })
      .defaultDestructor()

      // 可读写属性 x, y, z
      .property(
          "x",
          [](VM *vm, NativeInstance *inst) -> Value {
            return Value::number(inst->as<Vector3>()->x);
          },
          [](VM *vm, NativeInstance *inst, Value value) {
            inst->as<Vector3>()->x = value.isNumber() ? value.asNumber() : 0.0;
          })
      .property(
          "y",
          [](VM *vm, NativeInstance *inst) -> Value {
            return Value::number(inst->as<Vector3>()->y);
          },
          [](VM *vm, NativeInstance *inst, Value value) {
            inst->as<Vector3>()->y = value.isNumber() ? value.asNumber() : 0.0;
          })
      .property(
          "z",
          [](VM *vm, NativeInstance *inst) -> Value {
            return Value::number(inst->as<Vector3>()->z);
          },
          [](VM *vm, NativeInstance *inst, Value value) {
            inst->as<Vector3>()->z = value.isNumber() ? value.asNumber() : 0.0;
          })

      // 只读属性 length
      .propertyReadOnly("length",
                        [](VM *vm, NativeInstance *inst) -> Value {
                          return Value::number(inst->as<Vector3>()->length());
                        })

      // 方法: normalize (修改自身)
      .method(
          "normalize",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            inst->as<Vector3>()->normalize();
            return Value::nil();
          },
          0)

      // 方法: add (返回新 Vector3)
      .method(
          "add",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            if (argc < 1 || !argv[0].isNativeInstance()) {
              vm->throwError(Value::object(vm->allocateString("add expects a Vector3")));
              return Value::nil();
            }
            NativeInstance *other = static_cast<NativeInstance *>(argv[0].asGC());
            Vector3 *otherVec = other->safeCast<Vector3>();
            if (!otherVec) {
              vm->throwError(Value::object(vm->allocateString("add expects a Vector3")));
              return Value::nil();
            }
            Vector3 result = inst->as<Vector3>()->add(*otherVec);
            NativeInstance *resultInst =
                createNativeObject<Vector3>(vm, result.x, result.y, result.z);
            return Value::object(resultInst);
          },
          1)

      // 方法: dot (返回数值)
      .method(
          "dot",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            if (argc < 1 || !argv[0].isNativeInstance()) {
              return Value::number(0);
            }
            NativeInstance *other = static_cast<NativeInstance *>(argv[0].asGC());
            Vector3 *otherVec = other->safeCast<Vector3>();
            if (!otherVec)
              return Value::number(0);
            return Value::number(inst->as<Vector3>()->dot(*otherVec));
          },
          1)

      // 方法: scale (返回新 Vector3)
      .method(
          "scale",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            double s = (argc > 0 && argv[0].isNumber()) ? argv[0].asNumber() : 1.0;
            Vector3 result = inst->as<Vector3>()->scale(s);
            NativeInstance *resultInst =
                createNativeObject<Vector3>(vm, result.x, result.y, result.z);
            return Value::object(resultInst);
          },
          1)

      // 静态方法: zero
      .staticMethod(
          "zero",
          [](VM *vm, Value receiver, int argc, Value *argv) -> Value {
            NativeInstance *inst = createNativeObject<Vector3>(vm, 0, 0, 0);
            return Value::object(inst);
          },
          0)

      // 静态方法: one
      .staticMethod(
          "one",
          [](VM *vm, Value receiver, int argc, Value *argv) -> Value {
            NativeInstance *inst = createNativeObject<Vector3>(vm, 1, 1, 1);
            return Value::object(inst);
          },
          0)

      .build();
}

// ----------------------------------------------------------------------------
// Counter 类 - 简单的计数器，演示状态管理
// ----------------------------------------------------------------------------
class Counter {
public:
  int value;
  int step;

  Counter() : value(0), step(1) {}

  Counter(int initial, int step) : value(initial), step(step) {}

  void increment() { value += step; }

  void decrement() { value -= step; }

  void reset() { value = 0; }
};

inline void registerCounter(VM *vm) {
  NativeClassBuilder<Counter>(vm, "Counter")
      .constructor([](VM *vm, int argc, Value *argv) -> void * {
        int initial = (argc > 0 && argv[0].isInt()) ? static_cast<int>(argv[0].asInt()) : 0;
        int step = (argc > 1 && argv[1].isInt()) ? static_cast<int>(argv[1].asInt()) : 1;
        return new Counter(initial, step);
      })
      .defaultDestructor()

      .property(
          "value",
          [](VM *vm, NativeInstance *inst) -> Value {
            return Value::integer(inst->as<Counter>()->value);
          },
          [](VM *vm, NativeInstance *inst, Value value) {
            inst->as<Counter>()->value = value.isInt() ? static_cast<int>(value.asInt()) : 0;
          })

      .property(
          "step",
          [](VM *vm, NativeInstance *inst) -> Value {
            return Value::integer(inst->as<Counter>()->step);
          },
          [](VM *vm, NativeInstance *inst, Value value) {
            inst->as<Counter>()->step = value.isInt() ? static_cast<int>(value.asInt()) : 1;
          })

      .method(
          "increment",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            inst->as<Counter>()->increment();
            return Value::integer(inst->as<Counter>()->value);
          },
          0)

      .method(
          "decrement",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            inst->as<Counter>()->decrement();
            return Value::integer(inst->as<Counter>()->value);
          },
          0)

      .method(
          "reset",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            inst->as<Counter>()->reset();
            return Value::nil();
          },
          0)

      .build();
}

// ----------------------------------------------------------------------------
// StringBuffer 类 - 字符串缓冲区，演示字符串操作
// ----------------------------------------------------------------------------
class StringBuffer {
public:
  std::string buffer;

  StringBuffer() = default;

  explicit StringBuffer(const std::string &initial) : buffer(initial) {}

  void append(const std::string &str) { buffer += str; }

  void clear() { buffer.clear(); }

  size_t length() const { return buffer.length(); }

  std::string toString() const { return buffer; }
};

inline void registerStringBuffer(VM *vm) {
  NativeClassBuilder<StringBuffer>(vm, "StringBuffer")
      .constructor([](VM *vm, int argc, Value *argv) -> void * {
        if (argc > 0 && argv[0].isString()) {
          StringObject *str = static_cast<StringObject *>(argv[0].asGC());
          return new StringBuffer(str->data);
        }
        return new StringBuffer();
      })
      .defaultDestructor()

      .propertyReadOnly("length",
                        [](VM *vm, NativeInstance *inst) -> Value {
                          return Value::integer(inst->as<StringBuffer>()->length());
                        })

      .method(
          "append",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            if (argc > 0 && argv[0].isString()) {
              StringObject *str = static_cast<StringObject *>(argv[0].asGC());
              inst->as<StringBuffer>()->append(str->data);
            }
            return Value::object(inst); // 返回 this 支持链式调用
          },
          1)

      .method(
          "clear",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            inst->as<StringBuffer>()->clear();
            return Value::object(inst);
          },
          0)

      .method(
          "toString",
          [](VM *vm, NativeInstance *inst, int argc, Value *argv) -> Value {
            return Value::object(vm->allocateString(inst->as<StringBuffer>()->toString()));
          },
          0)

      .build();
}

// ----------------------------------------------------------------------------
// 注册所有原生绑定
// ----------------------------------------------------------------------------
inline void registerAllNativeBindings(VM *vm) {
  registerVector3(vm);
  registerCounter(vm);
  registerStringBuffer(vm);
}

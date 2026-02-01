#pragma once
#include "../TestRunner.h"
#include "../Vm/Object.h"
#include "../Vm/VM.h"

// =========================================================
// 原生绑定回归测试 (Native Bindings)
// =========================================================

inline void registerNativeBindingTests(TestRunner &runner) {

  // 1. 测试基本的整数加法
  runner.addNativeTest("Native Binding: Integer Add", "print(nativeAdd(10, 20));", "30",
                       [](VM *vm) {
                         vm->registerNative(
                             "nativeAdd",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               int64_t a = args[0].asInt();
                               int64_t b = args[1].asInt();
                               vm->push(Value::integer(a + b));
                               return 1;
                             },
                             2);
                       });

  // 2. 测试字符串拼接
  runner.addNativeTest("Native Binding: String Concat",
                       "print(nativeConcat(\"Hello\", \"World\"));", "Hello_World", [](VM *vm) {
                         vm->registerNative(
                             "nativeConcat",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               std::string s1 = args[0].asString()->str();
                               std::string s2 = args[1].asString()->str();
                               vm->push(Value::object(vm->allocateString(s1 + "_" + s2)));
                               return 1;
                             },
                             2);
                       });

  // 3. 测试浮点数运算
  runner.addNativeTest("Native Binding: Float Ops", "print(nativeSquare(1.5));", "2.25",
                       [](VM *vm) {
                         vm->registerNative(
                             "nativeSquare",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               double val = args[0].asFloat();
                               vm->push(Value::number(val * val));
                               return 1;
                             },
                             1);
                       });

  // 4. 测试布尔逻辑
  runner.addNativeTest("Native Binding: Bool Logic",
                       "print(nativeIsEven(4)); print(nativeIsEven(5));", "true\nfalse",
                       [](VM *vm) {
                         vm->registerNative(
                             "nativeIsEven",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               int64_t val = args[0].asInt();
                               vm->push(Value::boolean(val % 2 == 0));
                               return 1;
                             },
                             1);
                       });

  // 5. 测试 C++ 修改脚本全局变量
  runner.addNativeTest("Native Binding: Set Global",
                       R"(
      setSecret(999);
      print(secret);
    )",
                       "999", [](VM *vm) {
                         vm->registerNative(
                             "setSecret",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               vm->setGlobal("secret", args[0]);
                               return 0;
                             },
                             1);
                       });

  // 6. 测试 C++ 获取脚本全局变量
  runner.addNativeTest("Native Binding: Get Global",
                       R"(
      setTrueGlobal("config", 42);
      print(checkConfig());
    )",
                       "true", [](VM *vm) {
                         vm->registerNative(
                             "setTrueGlobal",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               std::string key = args[0].asString()->str();
                               vm->setGlobal(key, args[1]);
                               return 0;
                             },
                             2);

                         vm->registerNative(
                             "checkConfig",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               Value val = vm->getGlobal("config");
                               bool isCorrect = (val.isInt() && val.asInt() == 42);
                               vm->push(Value::boolean(isCorrect));
                               return 1;
                             },
                             0);
                       });
  // 7. 测试多返回值
  runner.addNativeTest("Native Binding: Multiple Returns",
                       R"(
      vars a, b = nativeSwap(1, 2);
      print(a);
      print(b);
    )",
                       "2\n1", [](VM *vm) {
                         vm->registerNative(
                             "nativeSwap",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               Value v1 = args[0];
                               Value v2 = args[1];
                               vm->push(v2);
                               vm->push(v1);
                               return 2;
                             },
                             2);
                       });

  // 8. 测试原生函数抛出运行时错误
  runner.addNativeFailTest("Native Binding: Runtime Error", "nativeCrash();", [](VM *vm) {
    vm->registerNative(
        "nativeCrash",
        [](VM *vm, Closure *, int argc, Value *args) -> int {
          vm->throwError(Value::object(vm->allocateString("Boom from C++!")));
          return 0;
        },
        0);
  });

  // 9. 变参函数测试
  runner.addNativeTest("Native Binding: Variadic", "print(nativeSum(1, 2, 3, 4, 5));", "15",
                       [](VM *vm) {
                         vm->registerNative(
                             "nativeSum",
                             [](VM *vm, Closure *, int argc, Value *args) -> int {
                               int64_t sum = 0;
                               for (int i = 0; i < argc; ++i) {
                                 if (args[i].isInt()) {
                                   sum += args[i].asInt();
                                 }
                               }
                               vm->push(Value::integer(sum));
                               return 1;
                             },
                             -1);
                       });
}
#pragma once
#include "NativeBindings.h"
#include "TestRunner.h"

// ============================================================================
// Native Binding Tests - 原生绑定测试
// ============================================================================

inline void registerNativeBindingTests(TestRunner &runner) {
  // Vector3 基础测试
  runner.addNativeTest("Native - Vector3 Basic",
                       R"(
        auto v = Vector3(1.0, 2.0, 3.0);
        print(v.x);
        print(v.y);
        print(v.z);
      )",
                       "1\n2\n3", registerVector3);

  // Vector3 属性修改
  runner.addNativeTest("Native - Vector3 Property Set",
                       R"(
        auto v = Vector3(0.0, 0.0, 0.0);
        v.x = 10.0;
        v.y = 20.0;
        v.z = 30.0;
        print(v.x);
        print(v.y);
        print(v.z);
      )",
                       "10\n20\n30", registerVector3);

  // Vector3 只读属性 length
  runner.addNativeTest("Native - Vector3 Length",
                       R"(
        auto v = Vector3(3.0, 4.0, 0.0);
        print(v.length);
      )",
                       "5", registerVector3);

  // Vector3 方法调用
  runner.addNativeTest("Native - Vector3 Add",
                       R"(
        auto v1 = Vector3(1.0, 2.0, 3.0);
        auto v2 = Vector3(4.0, 5.0, 6.0);
        auto v3 = v1.add(v2);
        print(v3.x);
        print(v3.y);
        print(v3.z);
      )",
                       "5\n7\n9", registerVector3);

  // Vector3 dot 方法
  runner.addNativeTest("Native - Vector3 Dot",
                       R"(
        auto v1 = Vector3(1.0, 2.0, 3.0);
        auto v2 = Vector3(4.0, 5.0, 6.0);
        print(v1.dot(v2));
      )",
                       "32", registerVector3);

  // Vector3 静态方法
  runner.addNativeTest("Native - Vector3 Static Methods",
                       R"(
        auto zero = Vector3.zero();
        auto one = Vector3.one();
        print(zero.x .. " " .. zero.y .. " " .. zero.z);
        print(one.x .. " " .. one.y .. " " .. one.z);
      )",
                       "0.000000 0.000000 0.000000\n1.000000 1.000000 1.000000", registerVector3);

  // Vector3 scale 方法
  runner.addNativeTest("Native - Vector3 Scale",
                       R"(
        auto v = Vector3(1.0, 2.0, 3.0);
        auto scaled = v.scale(2.0);
        print(scaled.x);
        print(scaled.y);
        print(scaled.z);
      )",
                       "2\n4\n6", registerVector3);
  runner.addNativeTest("Native - Vector3 Scale",
                       R"(
        auto v = Vector3(1.0, 2.0, 3.0);
        vars x, y, z = v.xyz();
        print(x);
        print(y);
        print(z);
      )",
                       "1\n2\n3", registerVector3);
  // Counter 基础测试
  runner.addNativeTest("Native - Counter Basic",
                       R"(
        auto c = Counter(0, 1);
        print(c.value);
        c.increment();
        print(c.value);
        c.increment();
        print(c.value);
        c.decrement();
        print(c.value);
      )",
                       "0\n1\n2\n1", registerCounter);

  // Counter 自定义步长
  runner.addNativeTest("Native - Counter Custom Step",
                       R"(
        auto c = Counter(10, 5);
        print(c.value);
        c.increment();
        print(c.value);
        c.increment();
        print(c.value);
        c.reset();
        print(c.value);
      )",
                       "10\n15\n20\n0", registerCounter);

  // Counter 属性修改
  runner.addNativeTest("Native - Counter Property Set",
                       R"(
        auto c = Counter(0, 1);
        c.value = 100;
        c.step = 10;
        c.increment();
        print(c.value);
      )",
                       "110", registerCounter);

  // StringBuffer 基础测试
  runner.addNativeTest("Native - StringBuffer Basic",
                       R"(
        auto sb = StringBuffer();
        sb.append("Hello");
        sb.append(" ");
        sb.append("World");
        print(sb.toString());
        print(sb.length);
      )",
                       "Hello World\n11", registerStringBuffer);

  // StringBuffer 初始值
  runner.addNativeTest("Native - StringBuffer Initial",
                       R"(
        auto sb = StringBuffer("Initial: ");
        sb.append("Value");
        print(sb.toString());
      )",
                       "Initial: Value", registerStringBuffer);

  // StringBuffer 链式调用
  runner.addNativeTest("Native - StringBuffer Chaining",
                       R"(
        auto sb = StringBuffer();
        sb.append("A").append("B").append("C");
        print(sb.toString());
      )",
                       "ABC", registerStringBuffer);

  // StringBuffer clear
  runner.addNativeTest("Native - StringBuffer Clear",
                       R"(
        auto sb = StringBuffer("Hello");
        print(sb.length);
        sb.clear();
        print(sb.length);
        sb.append("New");
        print(sb.toString());
      )",
                       "5\n0\nNew", registerStringBuffer);

  // 多个原生类组合测试
  runner.addNativeTest("Native - Multiple Classes",
                       R"(
        auto v1 = Vector3(1.0, 0.0, 0.0);
        auto v2 = Vector3(0.0, 1.0, 0.0);
        auto c = Counter(0, 1);
        auto sb = StringBuffer();

        auto v3 = v1.add(v2);
        c.increment();
        c.increment();
        sb.append("Result: ");
        sb.append(v3.x .. "," .. v3.y .. "," .. v3.z);
        sb.append(" Count: ");
        sb.append(toString(c.value));

        print(sb.toString());
      )",
                       "Result: 1.000000,1.000000,0.000000 Count: 2", registerAllNativeBindings);

  // 原生对象在循环中使用
  runner.addNativeTest("Native - Vector3 In Loop",
                       R"(
        var sum = Vector3.zero();
        for (int i = 1; i <= 3; i = i + 1) {
            var v = Vector3(toFloat(i), toFloat(i * 2), toFloat(i * 3));
            sum = sum.add(v);
        }
        print(toInt(sum.x));
        print(toInt(sum.y));
        print(toInt(sum.z));
      )",
                       "6\n12\n18", registerVector3);

  // 原生对象作为容器元素
  runner.addNativeTest("Native - Vector3 In List",
                       R"(
        list<any> vectors = [];
        vectors.push(Vector3(1.0, 0.0, 0.0));
        vectors.push(Vector3(0.0, 1.0, 0.0));
        vectors.push(Vector3(0.0, 0.0, 1.0));

        float total = 0.0;
        for (int i = 0; i < vectors.length; i = i + 1) {
            var v = vectors[i];
            total = total + v.length;
        }
        print(toInt(total));
      )",
                       "3", registerVector3);
}

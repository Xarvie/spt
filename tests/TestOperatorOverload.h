#pragma once
#include "Test.h"

inline void registerOperatorOverloadTest(Test &runner) {

  // =======================================================
  // 运算符重载基础测试 - 使用 Lua 标准写法
  // =======================================================

  // ---------------------------------------------------------
  // Vec2 类：加法运算符重载
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Vec2 __add",
                 R"(
            class Vec2 {
                int x;
                int y;
                void __init(int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                Vec2 __add(Vec2 other) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x + other.x;
                    result.y = this.y + other.y;
                    return result;
                }
            }
            Vec2 v1 = new Vec2(3, 4);
            Vec2 v2 = new Vec2(1, 2);
            Vec2 v3 = v1 + v2;
            print(v3.x);
            print(v3.y);
       )",
                 "4\n6");

  // ---------------------------------------------------------
  // Vec2 类：减法运算符重载
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Vec2 __sub",
                 R"(
            class Vec2 {
                int x;
                int y;
                void __init(int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                Vec2 __sub(Vec2 other) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x - other.x;
                    result.y = this.y - other.y;
                    return result;
                }
            }
            Vec2 v1 = new Vec2(5, 7);
            Vec2 v2 = new Vec2(2, 3);
            Vec2 v3 = v1 - v2;
            print(v3.x);
            print(v3.y);
       )",
                 "3\n4");

  // ---------------------------------------------------------
  // Vec2 类：乘法运算符重载（标量）
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Vec2 __mul scalar",
                 R"(
            class Vec2 {
                int x;
                int y;
                void __init(int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                Vec2 __mul(int scalar) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x * scalar;
                    result.y = this.y * scalar;
                    return result;
                }
            }
            Vec2 v = new Vec2(3, 4);
            Vec2 v2 = v * 2;
            print(v2.x);
            print(v2.y);
       )",
                 "6\n8");

  // ---------------------------------------------------------
  // Vec2 类：除法运算符重载（标量）
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Vec2 __div scalar",
                 R"(
            class Vec2 {
                int x;
                int y;
                void __init(int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                Vec2 __div(int scalar) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x / scalar;
                    result.y = this.y / scalar;
                    return result;
                }
            }
            Vec2 v = new Vec2(6, 8);
            Vec2 v2 = v / 2;
            print(v2.x);
            print(v2.y);
       )",
                 "3\n4");

  // ---------------------------------------------------------
  // Vec2 类：链式运算符
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Chained operations",
                 R"(
            class Vec2 {
                int x;
                int y;
                void __init(int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                Vec2 __add(Vec2 other) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x + other.x;
                    result.y = this.y + other.y;
                    return result;
                }
                Vec2 __mul(int scalar) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x * scalar;
                    result.y = this.y * scalar;
                    return result;
                }
            }
            Vec2 v1 = new Vec2(1, 1);
            Vec2 v2 = new Vec2(2, 2);
            Vec2 result = (v1 + v2) * 3;
            print(result.x);
            print(result.y);
       )",
                 "9\n9");

  // ---------------------------------------------------------
  // Number 类：包装整数的运算符重载
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Number wrapper add sub mul div",
                 R"(
            class Number {
                int value;
                void __init(int value) {
                    this.value = value;
                }
                Number __add(Number other) {
                    Number result = new Number(0);
                    result.value = this.value + other.value;
                    return result;
                }
                Number __sub(Number other) {
                    Number result = new Number(0);
                    result.value = this.value - other.value;
                    return result;
                }
                Number __mul(Number other) {
                    Number result = new Number(0);
                    result.value = this.value * other.value;
                    return result;
                }
                Number __div(Number other) {
                    Number result = new Number(0);
                    result.value = this.value / other.value;
                    return result;
                }
            }
            Number a = new Number(10);
            Number b = new Number(5);
            Number sum = a + b;
            Number diff = a - b;
            Number prod = a * b;
            Number quot = a / b;
            print(sum.value);
            print(diff.value);
            print(prod.value);
            print(quot.value);
       )",
                 "15\n5\n50\n2");

  // ---------------------------------------------------------
  // Vector3 类：完整的四则运算
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Vector3 full arithmetic",
                 R"(
            class Vector3 {
                int x;
                int y;
                int z;
                void __init(int x, int y, int z) {
                    this.x = x;
                    this.y = y;
                    this.z = z;
                }
                Vector3 __add(Vector3 other) {
                    Vector3 result = new Vector3(0, 0, 0);
                    result.x = this.x + other.x;
                    result.y = this.y + other.y;
                    result.z = this.z + other.z;
                    return result;
                }
                Vector3 __sub(Vector3 other) {
                    Vector3 result = new Vector3(0, 0, 0);
                    result.x = this.x - other.x;
                    result.y = this.y - other.y;
                    result.z = this.z - other.z;
                    return result;
                }
                Vector3 __mul(int scalar) {
                    Vector3 result = new Vector3(0, 0, 0);
                    result.x = this.x * scalar;
                    result.y = this.y * scalar;
                    result.z = this.z * scalar;
                    return result;
                }
                Vector3 __div(int scalar) {
                    Vector3 result = new Vector3(0, 0, 0);
                    result.x = this.x / scalar;
                    result.y = this.y / scalar;
                    result.z = this.z / scalar;
                    return result;
                }
            }
            Vector3 v1 = new Vector3(1, 2, 3);
            Vector3 v2 = new Vector3(4, 5, 6);
            Vector3 sum = v1 + v2;
            Vector3 diff = v1 - v2;
            Vector3 scaled = v1 * 2;
            Vector3 divided = v1 / 2;
            print(sum.x .. "," .. sum.y .. "," .. sum.z);
            print(diff.x .. "," .. diff.y .. "," .. diff.z);
            print(scaled.x .. "," .. scaled.y .. "," .. scaled.z);
            print(divided.x .. "," .. divided.y .. "," .. divided.z);
       )",
                 "5,7,9\n-3,-3,-3\n2,4,6\n0.5,1.0,1.5");

  // ---------------------------------------------------------
  // 一元运算符 __unm 测试
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Unary minus __unm",
                 R"(
            class Vec2 {
                int x;
                int y;
                void __init(int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                Vec2 __unm() {
                    Vec2 result = new Vec2(0, 0);
                    result.x = -this.x;
                    result.y = -this.y;
                    return result;
                }
            }
            Vec2 v = new Vec2(3, -4);
            Vec2 neg = -v;
            print(neg.x);
            print(neg.y);
       )",
                 "-3\n4");

  // ---------------------------------------------------------
  // 多个元方法组合测试
  // ---------------------------------------------------------
  runner.addTest("Operator Overload: Multiple metamethods combined",
                 R"(
            class Vec2 {
                int x;
                int y;
                void __init(int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                Vec2 __add(Vec2 other) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x + other.x;
                    result.y = this.y + other.y;
                    return result;
                }
                Vec2 __sub(Vec2 other) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x - other.x;
                    result.y = this.y - other.y;
                    return result;
                }
                Vec2 __mul(int scalar) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x * scalar;
                    result.y = this.y * scalar;
                    return result;
                }
                Vec2 __div(int scalar) {
                    Vec2 result = new Vec2(0, 0);
                    result.x = this.x / scalar;
                    result.y = this.y / scalar;
                    return result;
                }
            }
            Vec2 v1 = new Vec2(10, 20);
            Vec2 v2 = new Vec2(3, 4);
            // (v1 - v2) * 2 = (7, 16) * 2 = (14, 32)
            Vec2 diff = v1 - v2;
            Vec2 scaled = diff * 2;
            print(scaled.x);
            print(scaled.y);
       )",
                 "14\n32");
}
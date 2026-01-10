
#include "catch_amalgamated.hpp"

#include "TestRunner.h"

using namespace spt::test;

TEST_CASE("old benchmark all", "[benchmark][old]") {
  TestRunner runner;
  runner.runBenchmark("Particle Simulation",
                      R"(
            class Vector {
                float x;
                float y;

                void init(Vector this, float x, float y) {
                    this.x = x;
                    this.y = y;
                }

                void add(Vector this, float dx, float dy) {
                    this.x += dx;
                    this.y += dy;
                }
            }

            class Particle {
                Vector pos;
                Vector vel;
                int id;

                void init(Particle this, int id) {
                    this.id = id;
                    // 嵌套对象创建
                    this.pos = new Vector(0.0, 0.0);
                    this.vel = new Vector(1.5, 0.5);
                }

                void update(Particle this) {
                    // 核心热点：方法调用 (OP_INVOKE)
                    this.pos.add(this.vel.x, this.vel.y);

                    // 简单的边界反弹逻辑 (条件跳转)
                    if (this.pos.x > 100.0) {
                        this.pos.x = 0.0;
                    }
                    if (this.pos.y > 100.0) {
                        this.pos.y = 0.0;
                    }
                }

                float checksum(Particle this) {
                    return this.pos.x + this.pos.y;
                }
            }

            // 1. 初始化容器
            list<any> systems = [];
            int count = 500;

            for (int i = 0; i < count; i += 1) {
                systems.push(new Particle(i));
            }

            // 2. 主模拟循环
            int frames = 100;
            for (int f = 0; f < frames; f += 1) {
                for (int i = 0; i < systems.length; i += 1) {
                    Particle p = systems[i];
                    p.update();
                }
            }

            // 3. 验证结果
            float total = 0.0;
            for (int i = 0; i < systems.length; i += 1) {
                Particle p = systems[i];
                total += p.checksum();
            }

            print(toInt(total));
       )",
                      "49750");
}

TEST_CASE("map benchmark - basic operations", "[benchmark][map]") {
  TestRunner runner;

  runner.runBenchmark("Map Int Key - Insert and Access",
                      R"(
            map<int, int> m = {};
            int count = 10000;
            for (int i = 0; i < count; i += 1) {
                m[i] = i * 2;
            }
            int sum = 0;
            for (int i = 0; i < count; i += 1) {
                sum += m[i];
            }
            print(sum);
       )",
                      "99990000");

  runner.runBenchmark("Map String Key - Insert and Access",
                      R"(
            map<string, int> m = {};
            int count = 5000;
            for (int i = 0; i < count; i += 1) {
                string key = "key_" + toString(i);
                m[key] = i;
            }
            int sum = 0;
            for (int i = 0; i < count; i += 1) {
                string key = "key_" + toString(i);
                sum += m[key];
            }
            print(sum);
       )",
                      "12497500");

  runner.runBenchmark("Map Bool Key - Insert and Access",
                      R"(
            map<bool, int> m = {};
            m[true] = 0;
            m[false] = 0;
            int count = 5000;
            for (int i = 0; i < count; i += 1) {
                m[true] = m[true] + 1;
                m[false] = m[false] + 2;
            }
            print(m[true]);
            print(m[false]);
       )",
                      "5000\n10000");

  runner.runBenchmark("Map Float Key - Insert and Access",
                      R"(
            map<float, int> m = {};
            int count = 5000;
            for (int i = 0; i < count; i += 1) {
                m[toFloat(i) + 0.5] = i;
            }
            int sum = 0;
            for (int i = 0; i < count; i += 1) {
                sum += m[toFloat(i) + 0.5];
            }
            print(sum);
       )",
                      "12497500");

  runner.runBenchmark("Map Mixed Types - Insert and Access",
                      R"(
            map<any, int> m = {};
            int count = 3000;
            for (int i = 0; i < count; i += 1) {
                m[i] = i;
                string key = "str_" + i;
                m[key] = i * 2;
            }
            int sum = 0;
            for (int i = 0; i < count; i += 1) {
                sum += m[i];
                string key = "str_" + i;
                sum += m[key];
            }
            print(sum);
       )",
                      "13495500");
}

TEST_CASE("map benchmark - complex keys", "[benchmark][map]") {
  TestRunner runner;

  runner.runBenchmark("Map Function Key - Insert and Access",
                      R"(
            map<function, int> m = {};
            int count = 500;
            for (int i = 0; i < count; i += 1) {
                auto f = function(int x) -> int { return x + i; };
                m[f] = i;
            }
            int sum = 0;
            print(m.size);
       )",
                      "500");

  runner.runBenchmark("Map Mixed Complex Keys - Insert and Access",
                      R"(
            map<any, int> m = {};
            int count = 500;
            for (int i = 0; i < count; i += 1) {
                list<int> lk = [i, i + 1];
                map<string, int> mk = {"id": i};
                m[lk] = i;
                m[mk] = i * 2;
            }
            print(m.size);
       )",
                      "1000");
}

TEST_CASE("map benchmark - complex values", "[benchmark][map]") {
  TestRunner runner;
  runner.runBenchmark("Map List Value - Insert and Access",
                      R"(
            map<int, list<int>> m = {};
            int count = 2000;
            for (int i = 0; i < count; i += 1) {
                m[i] = [i, i * 2, i * 3];
            }
            int sum = 0;
            for (int i = 0; i < count; i += 1) {
                sum += m[i][0];
                sum += m[i][1];
                sum += m[i][2];
            }
            print(sum);
       )",
                      "11994000");

  runner.runBenchmark("Map Map Value - Insert and Access",
                      R"(
            map<int, map<string, int>> m = {};
            int count = 1000;
            for (int i = 0; i < count; i += 1) {
                m[i] = {"a": i, "b": i * 2};
            }
            int sum = 0;
            for (int i = 0; i < count; i += 1) {
                sum += m[i]["a"];
                sum += m[i]["b"];
            }
            print(sum);
       )",
                      "1498500");

  runner.runBenchmark("Map String Value - Insert and Access",
                      R"(
            map<int, string> m = {};
            int count = 5000;
            for (int i = 0; i < count; i += 1) {
                m[i] = "value_" + toString(i);
            }
            print(m.size);
            print(m[0]);
            print(m[4999]);
       )",
                      "5000\nvalue_0\nvalue_4999");

  runner.runBenchmark("Map Nested Structures - Insert and Access",
                      R"(
            map<int, list<map<string, int>>> m = {};
            int count = 500;
            for (int i = 0; i < count; i += 1) {
                list<map<string, int>> val = [{"x": i, "y": i * 2}, {"a": i, "b": i * 3}];
                m[i] = val;
            }
            int sum = 0;
            for (int i = 0; i < count; i += 1) {
                sum += m[i][0]["x"];
                sum += m[i][0]["y"];
                sum += m[i][1]["a"];
                sum += m[i][1]["b"];
            }
            print(sum);
       )",
                      "873250");
}
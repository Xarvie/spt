#pragma once
#include "TestRunner.h"

// =========================================================
// 性能基准测试 (Benchmarks)
// =========================================================

inline void registerBench(TestRunner &runner) {
  runner.addTest("Particle Simulation",
                 R"(
            class Vector {
                float x;
                float y;

                void init(float x, float y) {
                    this.x = x;
                    this.y = y;
                }

                void add(float dx, float dy) {
                    this.x += dx;
                    this.y += dy;
                }
            }

            class Particle {
                Vector pos;
                Vector vel;
                int id;

                void init(int id) {
                    this.id = id;
                    // 嵌套对象创建
                    this.pos = new Vector(0.0, 0.0);
                    this.vel = new Vector(1.5, 0.5);
                }

                void update() {
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

                float checksum() {
                    return this.pos.x + this.pos.y;
                }
            }

            // 1. 初始化容器
            list<any> systems = [];
            int count = 2000;

            for (int i = 0; i < count; i += 1) {
                systems.push(new Particle(i));
            }

            // 2. 主模拟循环
            int frames = 1000;
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
                 "382000");
}

inline void registerMapBench(TestRunner &runner) {
  runner.addTest("Map RW Bench",
                 R"(
             map<string, int> m = {};

             for (int i = 0; i < 10000000; i = i + 1) {
                m[toString(i)] = i;
                m[toString(i+1)] = m[toString(i)];
             }
             print(m.size);
       )",
                 "10000001");
}

inline void registerFib40Bench(TestRunner &runner) {
  runner.addTest("Recursion - Fibonacci",
                 R"(
            int fib(int n) {
                if (n < 2) { return n; }
                return fib(n-1) + fib(n-2);
            }
            print(fib(40));
       )",
                 "102334155");
}

inline void registerBench1(TestRunner &runner) {
  runner.addTest("Benchmark loopAdd",
                 R"spt(
// lua6.35s spt7.9s
int sum = 0;
int i = 0;
int limit = 1000000000;

while (i < limit) {
    sum = sum + i;
    i = i + 1;
}
       )spt",
                 "");
}

inline void registerBench2(TestRunner &runner) {
  runner.addTest("Benchmark LoopFunctionAdd", // lua18s spt18s
                 R"spt(
int add(int a, int b) {
    return a + b;
}

int sum = 0;
int i = 0;
int limit = 1000000000;

while (i < limit) {
    sum = add(sum, i);
    i = i + 1;
}
print(sum);
       )spt",
                 "499999999500000000");
}

inline void registerBench3(TestRunner &runner) {
  runner.addTest("Benchmark LoopMethodAdd", // lua 34s spt47s
                 R"spt(
class Adder {
    int sum;
    void __init(int sum){
        this.sum = sum;
    }

    void add(int i) {
        this.sum = this.sum + i;
    }
}

auto adder = new Adder(0);

auto i = 0;
auto limit = 1000000000;

while (i < limit) {
    adder.add(i);
    i = i + 1;
}
       )spt",
                 "");
}
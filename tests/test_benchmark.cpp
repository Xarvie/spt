
#include "catch_amalgamated.hpp"

#include "TestRunner.h"

using namespace spt::test;

TEST_CASE("old test all", "[benchmark][old]") {
  TestRunner runner;
  runner.runTest("Particle Simulation",
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
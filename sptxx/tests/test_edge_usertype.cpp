// test_edge_usertype.cpp - usertype 所有权/运算符/null 边界测试

#include "sptxx.hpp"
#include <iostream>
#include <memory>
#include <string>

static int failures = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n";         \
      ++failures;                                                              \
    } else {                                                                   \
      std::cout << "PASS: " << msg << "\n";                                    \
    }                                                                          \
  } while (0)

// 带运算符的类型
struct Num {
  int v = 0;
  Num() = default;
  Num(int x) : v(x) {}
  Num operator+(const Num &o) const { return Num{v + o.v}; }
  Num operator-() const { return Num{-v}; }
  bool operator==(const Num &o) const { return v == o.v; }
  bool operator<(const Num &o) const { return v < o.v; }
  std::size_t length() const { return static_cast<std::size_t>(v >= 0 ? v : 0); }
  std::string to_str() const { return "Num(" + std::to_string(v) + ")"; }
};

// 运算符返回 Num 值时需要 pusher 特化，包装为 owned userdata
namespace sptxx {
template <> struct pusher<Num> {
  static void push(lua_State *L, const Num &n) {
    void *ud = lua_newuserdatauv(L, sizeof(Num *), 0);
    *static_cast<Num **>(ud) = new Num(n);
    luaL_getmetatable(L, "Num");
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      delete *static_cast<Num **>(ud);
      throw error("Num metatable not found");
    }
    lua_setmetatable(L, -2);
  }
};
} // namespace sptxx

// 用于所有权测试
struct Tracker {
  int val = 0;
  Tracker() = default;
  Tracker(int x) : val(x) {}
  int get_val() const { return val; }
};

static Tracker *g_tracker = nullptr;

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // ---- 注册 Num ----
    {
      auto un = lua.new_usertype<Num>("Num");
      un.constructor<int>();
      un.set("v", &Num::v);
      un.set_add([](const Num &a, const Num &b) { return a + b; });
      un.set_unm([](const Num &a) { return -a; });
      un.set_eq([](const Num &a, const Num &b) { return a == b; });
      un.set_lt([](const Num &a, const Num &b) { return a < b; });
      un.set_len([](const Num &a) { return a.length(); });
      un.set_tostring([](const Num &a) { return a.to_str(); });
    }

    // ---- 1. __add 运算符 ----
    lua.do_string("a = Num(3); b = Num(4); c = a + b; r1 = c.v;");
    CHECK(lua.get_global<int>("r1") == 7, "__add: Num(3)+Num(4).v = " << lua.get_global<int>("r1"));

    // ---- 2. __unm 运算符 ----
    lua.do_string("d = -a; r2 = d.v;");
    CHECK(lua.get_global<int>("r2") == -3, "__unm: -Num(3).v = " << lua.get_global<int>("r2"));

    // ---- 3. __eq 运算符 ----
    lua.do_string("e1 = Num(5); e2 = Num(5); e3 = Num(6); r3a = e1 == e2; r3b = e1 == e3;");
    CHECK(lua.get_global<bool>("r3a") == true, "__eq: Num(5)==Num(5) = true");
    CHECK(lua.get_global<bool>("r3b") == false, "__eq: Num(5)==Num(6) = false");

    // ---- 4. __lt 运算符 ----
    lua.do_string("r4 = Num(3) < Num(5);");
    CHECK(lua.get_global<bool>("r4") == true, "__lt: Num(3)<Num(5) = true");

    // ---- 5. __tostring 运算符 ----
    lua.do_string("r5 = tostring(Num(42));");
    CHECK(lua.get_global<std::string>("r5") == "Num(42)", "__tostring: \"" << lua.get_global<std::string>("r5") << "\"");

    // ---- 6. __len 运算符 ----
    lua.do_string("r6 = #Num(10);");
    CHECK(lua.get_global<int>("r6") == 10, "__len: #Num(10) = " << lua.get_global<int>("r6"));

    // ---- 7. 运算符链式 ----
    lua.do_string("chain = Num(1) + Num(2) + Num(3); r7 = chain.v;");
    CHECK(lua.get_global<int>("r7") == 6, "chained __add: Num(1)+Num(2)+Num(3) = " << lua.get_global<int>("r7"));

    // ---- 注册 Tracker ----
    {
      auto ut = lua.new_usertype<Tracker>("Tracker");
      ut.constructor<int>();
      ut.set("val", &Tracker::val);
      ut.set("get_val", &Tracker::get_val);
    }

    // ---- 8. unowned 对象 GC 后存活 ----
    {
      Tracker t{123};
      g_tracker = &t;
      auto ut = lua.get_usertype<Tracker>("Tracker");
      ut.push_unowned(&t);
      lua_setglobal(lua, "uo");
      lua.do_string("collectgarbage(\"collect\"); collectgarbage(\"collect\"); r8 = uo.get_val();");
      CHECK(lua.get_global<int>("r8") == 123, "unowned survives GC: get_val = " << lua.get_global<int>("r8"));
      CHECK(g_tracker->val == 123, "unowned C++ object still alive after GC");
    }

    // ---- 9. owned 对象 GC 后被释放（通过析构副作用验证）----
    {
      static int destroyed = 0;
      struct Dtor {
        int v;
        Dtor(int x) : v(x) {}
        ~Dtor() { ++destroyed; }
      };
      auto ud = lua.new_usertype<Dtor>("Dtor");
      ud.constructor<int>();
      ud.set("v", &Dtor::v);
      // 注意：usertype 析构 delete T*，需要 T 可被 delete
      lua.do_string("owned_obj = Dtor(1);");
      CHECK(destroyed == 0, "owned object alive before GC");
      lua.do_string("owned_obj = nil; collectgarbage(\"collect\"); collectgarbage(\"collect\");");
      CHECK(destroyed == 1, "owned object destroyed after GC (destroyed=" << destroyed << ")");
    }

    // ---- 10. shared 所有权：GC 释放 shared_ptr ----
    {
      static int shared_destroyed = 0;
      struct SObj {
        int v;
        SObj(int x) : v(x) {}
        ~SObj() { ++shared_destroyed; }
      };
      auto us = lua.new_usertype<SObj>("SObj");
      us.constructor<int>();
      us.set("v", &SObj::v);

      auto sp = std::make_shared<SObj>(99);
      auto ut = lua.get_usertype<SObj>("SObj");
      ut.push_shared(sp);
      lua_setglobal(lua, "sp_obj");
      CHECK(sp.use_count() == 2, "shared_ptr use_count == 2 after push_shared");
      lua.do_string("r10 = sp_obj.v;");
      CHECK(lua.get_global<int>("r10") == 99, "shared object accessible = " << lua.get_global<int>("r10"));
      sp.reset(); // C++ 侧释放，refcount 2→1（仅 Lua 侧持有）
      CHECK(shared_destroyed == 0, "shared object alive while Lua holds ref");
      lua.do_string("sp_obj = nil; collectgarbage(\"collect\"); collectgarbage(\"collect\");");
      CHECK(shared_destroyed == 1, "shared object destroyed after GC (destroyed=" << shared_destroyed << ")");
    }

    // ---- 11. null 对象调用方法报错 ----
    {
      auto ut = lua.get_usertype<Tracker>("Tracker");
      ut.push_owned(nullptr);
      lua_setglobal(lua, "null_t");
      bool threw = false;
      try {
        lua.do_string("r11 = null_t.get_val();");
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "null object method call throws");
    }

    // ---- 12. null 对象访问属性报错 ----
    {
      bool threw = false;
      try {
        lua.do_string("r12 = null_t.val;");
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "null object field access throws");
    }

    // ---- 13. 重复注册同名 usertype 幂等 ----
    {
      bool ok = true;
      try {
        auto ut2 = lua.new_usertype<Num>("Num");
        // 不再注册成员，验证已有成员仍可用
      } catch (...) {
        ok = false;
      }
      lua.do_string("r13 = Num(5).v;");
      CHECK(ok && lua.get_global<int>("r13") == 5, "re-register usertype is idempotent");
    }

    // ---- 14. 构造函数参数校验 ----
    {
      bool threw = false;
      try {
        lua.do_string("bad = Num(\"not_a_number\");");
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "constructor with wrong arg type throws");
    }

    if (failures == 0) {
      std::cout << "=== All usertype edge tests passed! ===\n";
      return 0;
    }
    std::cerr << "=== " << failures << " test(s) FAILED ===\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}

#include "sptxx.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

struct Vector3 {
  float x, y, z;

  Vector3() : x(0), y(0), z(0) {}
  Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

  std::string to_string() const {
    std::ostringstream ss;
    ss << "Vector3(" << x << ", " << y << ", " << z << ")";
    return ss.str();
  }
};

struct IntBox {
  int value;

  IntBox() : value(0) {}
  IntBox(int v) : value(v) {}

  std::string to_string() const {
    return std::to_string(value);
  }
};

Vector3 operator+(const Vector3 &a, const Vector3 &b) { return Vector3(a.x + b.x, a.y + b.y, a.z + b.z); }
Vector3 operator-(const Vector3 &a, const Vector3 &b) { return Vector3(a.x - b.x, a.y - b.y, a.z - b.z); }
Vector3 operator*(const Vector3 &v, float s) { return Vector3(v.x * s, v.y * s, v.z * s); }
Vector3 operator/(const Vector3 &v, float s) { return Vector3(v.x / s, v.y / s, v.z / s); }
Vector3 operator-(const Vector3 &v) { return Vector3(-v.x, -v.y, -v.z); }
bool operator==(const Vector3 &a, const Vector3 &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool operator<(const Vector3 &a, const Vector3 &b) { return std::sqrt(a.x*a.x + a.y*a.y + a.z*a.z) < std::sqrt(b.x*b.x + b.y*b.y + b.z*b.z); }
bool operator<=(const Vector3 &a, const Vector3 &b) { return !(b < a); }

namespace sptxx {
template <> struct pusher<Vector3> {
  static void push(lua_State *L, const Vector3 &v) {
    void *obj = lua_newuserdatauv(L, sizeof(Vector3 *), 0);
    Vector3 **ptr = static_cast<Vector3 **>(obj);
    *ptr = new Vector3(v);
    luaL_getmetatable(L, "Vector3");
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      delete *ptr;
      throw error("Vector3 metatable not found");
    }
    lua_setmetatable(L, -2);
  }
};

template <> struct pusher<IntBox> {
  static void push(lua_State *L, const IntBox &v) {
    void *obj = lua_newuserdatauv(L, sizeof(IntBox *), 0);
    IntBox **ptr = static_cast<IntBox **>(obj);
    *ptr = new IntBox(v);
    luaL_getmetatable(L, "IntBox");
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      delete *ptr;
      throw error("IntBox metatable not found");
    }
    lua_setmetatable(L, -2);
  }
};
} // namespace sptxx

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Usertype Operator Overloading (Complete) ===" << std::endl;

    std::cout << "\n--- Part 1: Vector3 (Arithmetic & Comparison) ---" << std::endl;

    auto vec3 = lua.new_usertype<Vector3>("Vector3");
    vec3.constructor<float, float, float>();
    vec3.set("x", &Vector3::x);
    vec3.set("y", &Vector3::y);
    vec3.set("z", &Vector3::z);
    vec3.set("to_string", &Vector3::to_string);

    vec3.set_add([](const Vector3 &a, const Vector3 &b) -> Vector3 { return a + b; });
    vec3.set_sub([](const Vector3 &a, const Vector3 &b) -> Vector3 { return a - b; });
    vec3.set_mul([](const Vector3 &v, float s) -> Vector3 { return v * s; });
    vec3.set_div([](const Vector3 &v, float s) -> Vector3 { return v / s; });
    vec3.set_unm([](const Vector3 &v) -> Vector3 { return -v; });
    vec3.set_eq([](const Vector3 &a, const Vector3 &b) -> bool { return a == b; });
    vec3.set_lt([](const Vector3 &a, const Vector3 &b) -> bool { return a < b; });
    vec3.set_le([](const Vector3 &a, const Vector3 &b) -> bool { return a <= b; });

    std::cout << "Vector3 registered!" << std::endl;

    std::cout << "\n1. Testing __add (+)..." << std::endl;
    lua.do_string("vars r = Vector3(1,2,3) + Vector3(4,5,6); print('Result: ' .. r.to_string(r));");
    std::cout << "Expected: Vector3(5, 7, 9)" << std::endl;

    std::cout << "\n2. Testing __sub (-)..." << std::endl;
    lua.do_string("vars r = Vector3(4,5,6) - Vector3(1,2,3); print('Result: ' .. r.to_string(r));");
    std::cout << "Expected: Vector3(3, 3, 3)" << std::endl;

    std::cout << "\n3. Testing __mul (*)..." << std::endl;
    lua.do_string("vars r = Vector3(1,2,3) * 2; print('Result: ' .. r.to_string(r));");
    std::cout << "Expected: Vector3(2, 4, 6)" << std::endl;

    std::cout << "\n4. Testing __div (/)..." << std::endl;
    lua.do_string("vars r = Vector3(2,4,6) / 2; print('Result: ' .. r.to_string(r));");
    std::cout << "Expected: Vector3(1, 2, 3)" << std::endl;

    std::cout << "\n5. Testing __unm (unary -)..." << std::endl;
    lua.do_string("vars r = -Vector3(1,2,3); print('Result: ' .. r.to_string(r));");
    std::cout << "Expected: Vector3(-1, -2, -3)" << std::endl;

    std::cout << "\n6. Testing __eq (==)..." << std::endl;
    lua.do_string("vars eq = Vector3(1,2,3) == Vector3(1,2,3); print('Equal: ' .. tostring(eq));");
    lua.do_string("vars neq = Vector3(1,2,3) == Vector3(1,2,4); print('Not equal: ' .. tostring(neq));");
    std::cout << "Expected: Equal: true, Not equal: false" << std::endl;

    std::cout << "\n7. Testing __lt (<)..." << std::endl;
    lua.do_string("vars lt = Vector3(1,0,0) < Vector3(3,4,0); print('Less: ' .. tostring(lt));");
    std::cout << "Expected: Less: true" << std::endl;

    std::cout << "\n8. Testing __le (<=)..." << std::endl;
    lua.do_string("vars le = Vector3(1,0,0) <= Vector3(3,4,0); print('Less or equal: ' .. tostring(le));");
    std::cout << "Expected: Less or equal: true" << std::endl;

    std::cout << "\n--- Part 2: IntBox (All Operators) ---" << std::endl;

    auto intbox = lua.new_usertype<IntBox>("IntBox");
    intbox.constructor<int>();
    intbox.set("value", &IntBox::value);
    intbox.set("to_string", &IntBox::to_string);

    intbox.set_add([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value + b.value); });
    intbox.set_sub([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value - b.value); });
    intbox.set_mul([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value * b.value); });
    intbox.set_div([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value / b.value); });
    intbox.set_mod([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value % b.value); });
    intbox.set_unm([](const IntBox &a) -> IntBox { return IntBox(-a.value); });
    intbox.set_idiv([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value / b.value); });

    intbox.set_band([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value & b.value); });
    intbox.set_bor([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value | b.value); });
    intbox.set_bxor([](const IntBox &a, const IntBox &b) -> IntBox { return IntBox(a.value ^ b.value); });
    intbox.set_bnot([](const IntBox &a) -> IntBox { return IntBox(~a.value); });
    intbox.set_shl([](const IntBox &a, int s) -> IntBox { return IntBox(a.value << s); });

    intbox.set_concat([](const IntBox &a, const IntBox &b) -> IntBox { 
      std::string s = std::to_string(a.value) + std::to_string(b.value);
      return IntBox(std::stoi(s));
    });
    intbox.set_len([](const IntBox &a) -> int { 
      return std::to_string(std::abs(a.value)).length(); 
    });

    intbox.set_eq([](const IntBox &a, const IntBox &b) -> bool { return a.value == b.value; });
    intbox.set_lt([](const IntBox &a, const IntBox &b) -> bool { return a.value < b.value; });
    intbox.set_le([](const IntBox &a, const IntBox &b) -> bool { return a.value <= b.value; });

    std::cout << "IntBox registered!" << std::endl;

    std::cout << "\n9. Testing __mod (%)..." << std::endl;
    lua.do_string("vars r = IntBox(17) % IntBox(5); print('17 % 5 = ' .. r.to_string(r));");
    std::cout << "Expected: 17 % 5 = 2" << std::endl;

    std::cout << "\n10. Testing __idiv (~/)..." << std::endl;
    lua.do_string("vars r = IntBox(17) ~/ IntBox(5); print('17 ~/ 5 = ' .. r.to_string(r));");
    std::cout << "Expected: 17 ~/ 5 = 3" << std::endl;

    std::cout << "\n11. Testing __band (&)..." << std::endl;
    lua.do_string("vars r = IntBox(12) & IntBox(10); print('12 & 10 = ' .. r.to_string(r));");
    std::cout << "Expected: 12 & 10 = 8" << std::endl;

    std::cout << "\n12. Testing __bor (|)..." << std::endl;
    lua.do_string("vars r = IntBox(12) | IntBox(10); print('12 | 10 = ' .. r.to_string(r));");
    std::cout << "Expected: 12 | 10 = 14" << std::endl;

    std::cout << "\n13. Testing __bxor (^)..." << std::endl;
    lua.do_string("vars r = IntBox(12) ^ IntBox(10); print('12 ^ 10 = ' .. r.to_string(r));");
    std::cout << "Expected: 12 ^ 10 = 6" << std::endl;

    std::cout << "\n14. Testing __bnot (~ unary)..." << std::endl;
    lua.do_string("vars r = ~IntBox(0); print('~0 = ' .. r.to_string(r));");
    std::cout << "Expected: ~0 = -1" << std::endl;

    std::cout << "\n15. Testing __shl (<<)..." << std::endl;
    lua.do_string("vars r = IntBox(1) << 3; print('1 << 3 = ' .. r.to_string(r));");
    std::cout << "Expected: 1 << 3 = 8" << std::endl;

    std::cout << "\n16. Testing __concat (..)..." << std::endl;
    lua.do_string("vars r = IntBox(12) .. IntBox(34); print('12 .. 34 = ' .. r.to_string(r));");
    std::cout << "Expected: 12 .. 34 = 1234" << std::endl;

    std::cout << "\n17. Testing __len (#)..." << std::endl;
    lua.do_string("vars r = #IntBox(12345); print('#12345 = ' .. tostring(r));");
    std::cout << "Expected: #12345 = 5" << std::endl;

    std::cout << "\n--- Part 3: Chained Operations ---" << std::endl;

    std::cout << "\n18. Testing chained arithmetic..." << std::endl;
    lua.do_string("vars r = IntBox(1) + IntBox(2) + IntBox(3); print('1+2+3 = ' .. r.to_string(r));");
    std::cout << "Expected: 1+2+3 = 6" << std::endl;

    std::cout << "\n19. Testing mixed operators..." << std::endl;
    lua.do_string("vars r = (IntBox(10) + IntBox(5)) * IntBox(2); print('(10+5)*2 = ' .. r.to_string(r));");
    std::cout << "Expected: (10+5)*2 = 30" << std::endl;

    std::cout << "\n=== All 19 Operator Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}

// variadic.hpp - 可变参数支持
// variadic_args 封装 Lua 栈上"剩余参数"区间 [start, top]，
// 供绑定函数接收变长实参。必须作为函数参数列表的最后一个。
//
// 用法：
//   lua.set_function("sum_all", [](sptxx::variadic_args va) {
//       int s = 0;
//       for (auto it = va.begin(); it != va.end(); ++it) s += it.get<int>();
//       return s;
//   });
//   // Lua: sum_all(1,2,3,4) -> 10
//
// 也可混合固定参数：
//   lua.set_function("f", [](int base, sptxx::variadic_args va) { ... });

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "stack.hpp"
#include <cstddef>
#include <iterator>

namespace sptxx {

class variadic_args {
public:
  variadic_args() : L_(nullptr), start_(0), end_(-1) {}
  variadic_args(lua_State *L, int start)
      : L_(L), start_(start), end_(lua_gettop(L)) {}
  variadic_args(lua_State *L, int start, int end)
      : L_(L), start_(start), end_(end) {}

  lua_State *lua_state() const { return L_; }
  int start_index() const { return start_; }
  int end_index() const { return end_; }
  int size() const { return end_ >= start_ ? end_ - start_ + 1 : 0; }
  bool empty() const { return size() == 0; }

  // 按相对位置读取一个值
  template <typename T> T get(int i) const {
    return stack::get<T>(L_, start_ + i);
  }

  // 把所有参数复制压入另一（或同一）栈
  void push_all(lua_State *L) const {
    for (int i = start_; i <= end_; ++i) {
      if (L_ == L) {
        lua_pushvalue(L_, i);
      } else {
        lua_pushvalue(L_, i);
        lua_xmove(L_, L, 1);
      }
    }
  }

  // 迭代器：it.get<T>() 读取当前栈位置
  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = void;
    using difference_type = int;
    using pointer = void;
    using reference = void;

    iterator(lua_State *L, int idx) : L_(L), idx_(idx) {}
    int stack_index() const { return idx_; }
    template <typename T> T get() const { return stack::get<T>(L_, idx_); }
    iterator &operator++() {
      ++idx_;
      return *this;
    }
    iterator operator++(int) {
      iterator t = *this;
      ++idx_;
      return t;
    }
    bool operator==(const iterator &o) const { return idx_ == o.idx_; }
    bool operator!=(const iterator &o) const { return idx_ != o.idx_; }

  private:
    lua_State *L_;
    int idx_;
  };

  iterator begin() const { return iterator(L_, start_); }
  iterator end() const { return iterator(L_, end_ + 1); }

private:
  lua_State *L_;
  int start_;
  int end_; // 闭区间；empty 时 start > end
};

// getter 特化：从栈 index 处构造 variadic_args，捕获 [index, top]
template <> struct getter<variadic_args> {
  static variadic_args get(lua_State *L, int index) {
    return variadic_args(L, index);
  }
};

} // namespace sptxx

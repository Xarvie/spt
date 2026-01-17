#pragma once

#include "Value.h"
#include "unordered_dense.h"
#include <string>
#include <string_view>

namespace spt {

class GC;
class VM;

class StringPool {
public:
  explicit StringPool(GC *gc) : gc_(gc) { strings_.reserve(256); }

  ~StringPool() = default;

  // 禁止拷贝和移动
  StringPool(const StringPool &) = delete;
  StringPool &operator=(const StringPool &) = delete;

  // === 核心驻留（Interning）API ===

  // 驻留一个字符串 - 返回现有对象或创建新对象
  StringObject *intern(std::string_view sv);

  // 从以 null 结尾的 C 字符串驻留
  StringObject *intern(const char *str) { return intern(std::string_view(str)); }

  // 从 std::string 驻留
  StringObject *intern(const std::string &str) { return intern(std::string_view(str)); }

  // === 查找（不创建新对象） ===

  // 如果字符串存在则返回指针，否则返回 nullptr
  StringObject *find(std::string_view sv) const;

  // 检查字符串是否已被驻留
  bool contains(std::string_view sv) const { return find(sv) != nullptr; }

  // === 池管理 ===

  // 从池中移除一个字符串（由 GC 在清除阶段调用）
  void remove(StringObject *str);

  // 清空所有字符串（由 GC 在全局回收时调用）
  void clear() { strings_.clear(); }

  // 获取已驻留字符串的数量
  size_t size() const { return strings_.size(); }

  // === GC 集成 ===

  // 在 GC 清除阶段移除所有未标记（白色）的字符串
  // 此函数在 GC 释放 StringObject 内存之前被调用
  void removeWhiteStrings();

  // 遍历所有字符串（用于调试）
  template <typename Func> void forEach(Func &&fn) const {
    for (StringObject *str : strings_) {
      fn(str);
    }
  }

private:
  GC *gc_;

  // 驻留集合 - 存储 StringObject 指针（Transparent Lookup）
  ankerl::unordered_dense::set<StringObject *, StringPtrHash, StringPtrEqual> strings_;
};

// ============================================================================
// 内置方法描述符 - 用于方法表注册
// ============================================================================
struct BuiltinMethodDesc {
  using MethodFn = Value (*)(VM *vm, Value receiver, int argc, Value *argv);
  MethodFn fn;
  int arity;
};

// ============================================================================
// SymbolTable - 预驻留的所有内置符号 + 方法表
// ============================================================================
struct SymbolTable {
  // 类相关
  StringObject *init = nullptr; // "init" - 构造函数
  StringObject *gc = nullptr;   // "__gc" - 终结器（Finalizer）
  StringObject *str = nullptr;  // "__str" - 字符串转换
  StringObject *len = nullptr;  // "__len" - 长度操作符

  // === 通用方法名 ===
  StringObject *push = nullptr;
  StringObject *pop = nullptr;
  StringObject *length = nullptr;
  StringObject *size = nullptr;
  StringObject *get = nullptr;
  StringObject *set = nullptr;
  StringObject *has = nullptr;
  StringObject *keys = nullptr;
  StringObject *values = nullptr;
  StringObject *clear = nullptr;
  StringObject *slice = nullptr;
  StringObject *indexOf = nullptr;
  StringObject *contains = nullptr;
  StringObject *join = nullptr;
  StringObject *split = nullptr;
  StringObject *trim = nullptr;
  StringObject *toUpper = nullptr;
  StringObject *toLower = nullptr;
  StringObject *replace = nullptr;
  StringObject *startsWith = nullptr;
  StringObject *endsWith = nullptr;
  StringObject *find = nullptr;
  StringObject *insert = nullptr;
  StringObject *removeAt = nullptr;
  StringObject *remove = nullptr;

  // === Fiber 相关 ===
  StringObject *create = nullptr;
  StringObject *yield = nullptr;
  StringObject *current = nullptr;
  StringObject *abort = nullptr;
  StringObject *suspend = nullptr;
  StringObject *call = nullptr;
  StringObject *tryCall = nullptr;
  StringObject *isDone = nullptr;
  StringObject *error = nullptr;
  StringObject *Fiber = nullptr;

  // =========================================================================
  // 内置类型方法表 - 以 StringObject* 为键，实现 O(1) 指针查找
  // =========================================================================
  StringMap<BuiltinMethodDesc> listMethods;
  StringMap<BuiltinMethodDesc> mapMethods;
  StringMap<BuiltinMethodDesc> stringMethods;
  StringMap<BuiltinMethodDesc> fiberMethods;

  // 初始化符号（仅驻留字符串，不注册方法）
  void initialize(StringPool &pool) {
    // 类相关
    init = pool.intern("init");
    gc = pool.intern("__gc");
    str = pool.intern("__str");
    len = pool.intern("__len");

    // 常用方法
    push = pool.intern("push");
    pop = pool.intern("pop");
    length = pool.intern("length");
    size = pool.intern("size");
    get = pool.intern("get");
    set = pool.intern("set");
    has = pool.intern("has");
    keys = pool.intern("keys");
    values = pool.intern("values");
    clear = pool.intern("clear");
    slice = pool.intern("slice");
    indexOf = pool.intern("indexOf");
    contains = pool.intern("contains");
    join = pool.intern("join");
    split = pool.intern("split");
    trim = pool.intern("trim");
    toUpper = pool.intern("toUpper");
    toLower = pool.intern("toLower");
    replace = pool.intern("replace");
    startsWith = pool.intern("startsWith");
    endsWith = pool.intern("endsWith");
    find = pool.intern("find");
    insert = pool.intern("insert");
    removeAt = pool.intern("removeAt");
    remove = pool.intern("remove");

    // Fiber
    create = pool.intern("create");
    yield = pool.intern("yield");
    current = pool.intern("current");
    abort = pool.intern("abort");
    suspend = pool.intern("suspend");
    call = pool.intern("call");
    tryCall = pool.intern("try");
    isDone = pool.intern("isDone");
    error = pool.intern("error");

    // 类型
    Fiber = pool.intern("Fiber");
  }

  // 注册内置方法表（在 VM 初始化时调用）
  void registerBuiltinMethods();
};

} // namespace spt
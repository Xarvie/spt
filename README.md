# SPT Lua 5.5 技术规范

> 本文档为SPT Lua 5.5 的关键技术规范，重点标注与标准 Lua 的差异和易错点。

---

## 1. 核心差异速查

| 特性          | 标准 Lua 5.5                 | SPT Lua 5.5               |
|-------------|----------------------------|---------------------------|
| 函数参数起始索引    | 索引 1                       | **索引 2**（索引 1 是 Receiver） |
| 数组索引        | 1-based                    | **0-based**               |
| 数组/表        | 统一 Table 类型                | **List / Map 分离**         |
| Registry 引用 | `lua_rawgeti(L, REG, ref)` | **`lua_getref(L, ref)`**  |
| `#` 对 Map   | 返回边界                       | **固定返回 0**                |
| 数组越界        | 返回 nil                     | **抛出错误**                  |

---

## 2. Slot 0 Receiver 调用约定（最重要）

### 2.1 基本规则

**所有函数调用，索引 1 永远是 Receiver，实际参数从索引 2 开始。**

```
栈布局：
  [1] receiver (Slot 0) - 始终存在
  [2] arg1              - 第一个实际参数
  [3] arg2              - 第二个实际参数
```

### 2.2 不同调用场景

| 调用方式                   | 栈布局                  | 参数起始索引      |
|------------------------|----------------------|-------------|
| `func(a, b)`           | `[nil, a, b]`        | **索引 2**    |
| `obj:method(a)`        | `[obj, a]`           | **索引 2**    |
| `obj.method(a)`        | `[obj, a]`           | **索引 2**    |
| `obj[key](a)`          | `[obj, a]`           | **索引 2**    |
| `Table(a, b)` (__call) | `[Table, nil, a, b]` | **索引 3** ⚠️ |
| `new Class(a, b)`      | `[Class, nil, a, b]` | **索引 3** ⚠️ |

### 2.3 C 函数模板

```c
/* 普通函数 / 方法 */
static int my_func(lua_State *L) {
  int arg1 = luaL_checkinteger(L, 2);  /* 从索引 2 开始 */
  int arg2 = luaL_checkinteger(L, 3);
  lua_pushinteger(L, result);
  return 1;
}

/* __call 元方法（构造函数） */
static int constructor(lua_State *L) {
  /* 索引 1: 表本身, 索引 2: nil */
  const char *name = luaL_checkstring(L, 3);  /* 从索引 3 开始！ */
  int val = luaL_checkinteger(L, 4);
  ...
}
```

### 2.4 ⚠️ 易错点：__call 参数索引

`__call` 元方法中，**参数从索引 3 开始**，不是索引 2！

原因：Lua VM 触发 `__call` 时，将被调用的表插入到 func 位置，导致：

- 索引 1 = 表本身（作为 Receiver）
- 索引 2 = 原本的 nil receiver
- 索引 3+ = 实际参数

---

## 3. List 与 Map 分离

### 3.1 类型判断

```c
#define ttisarray(o)  // 判断是否为 List
#define ttistable(o)  // 判断是否为 Map
```

### 3.2 行为差异

| 操作   | List (`LUA_VARRAY`)       | Map (`LUA_VTABLE`)         |
|------|---------------------------|----------------------------|
| 创建   | `lua_createarray(L, cap)` | `lua_createtable(L, 0, 0)` |
| 索引   | 仅整数，**0-based**           | 任意类型                       |
| `#t` | 返回 `loglen`               | **返回 0**                   |
| 越界访问 | **抛出错误**                  | 返回 nil                     |
| 越界写入 | **抛出错误**                  | 正常插入                       |

### 3.3 List 边界检查

```c
/* 以下操作会抛出错误 */
arr[-1]      // 负数索引
arr[100]     // 越界（>= loglen）
arr[1.5]     // 浮点索引
```

### 3.4 List API

#### 基础 API

```c
void lua_createarray(lua_State *L, int narray);
// 创建 List，初始容量为 narray，逻辑长度为 0

lua_Integer lua_arraylen(lua_State *L, int idx);
// 获取逻辑长度 (loglen)

void lua_arraysetlen(lua_State *L, int idx, lua_Integer newlen);
// 设置逻辑长度（缩容时清理截断槽位，保证 GC 安全）

void lua_arrayresize(lua_State *L, int idx, lua_Integer newsize);
// 调整物理容量（不能小于当前 loglen）
```

#### 扩展 API

```c
lua_Integer lua_arraycapacity(lua_State *L, int idx);
// 获取物理容量 (asize)

int lua_gettablemode(lua_State *L, int idx);
// 返回 TABLE_ARRAY(1) 或 TABLE_MAP(2)，无效类型返回 0

int lua_ismap(lua_State *L, int idx);
// 是否为 Map（TABLE_MAP 模式）

void lua_getarrayrange(lua_State *L, int idx, lua_Integer start, lua_Integer end);
// 获取 [start, end) 范围的元素，压入栈顶

void lua_setarrayrange(lua_State *L, int idx, lua_Integer start, lua_Integer count);
// 从栈顶弹出 count 个值，设置到 start 开始的位置

void lua_movearray(lua_State *L, int fromidx, int toidx, 
                   lua_Integer from, lua_Integer to, lua_Integer count);
// 在两个数组间移动元素

int lua_nextarray(lua_State *L, int idx, lua_Integer *cursor);
// 迭代数组，cursor 初始为 -1，返回 1 表示还有元素，0 表示结束
// 每次调用将 (index, value) 压入栈顶

int lua_arrayisempty(lua_State *L, int idx);
// 是否为空 (loglen == 0)

void lua_arrayreserve(lua_State *L, int idx, lua_Integer cap);
// 预留容量（只增不减）
```

---

## 4. Registry 引用机制（破坏性变更）

### 4.1 ⚠️ 禁止使用的 API

```c
// ❌ 禁止！会导致未定义行为
lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
lua_rawseti(L, LUA_REGISTRYINDEX, ref);
```

### 4.2 正确用法

```c
int ref = luaL_ref(L, LUA_REGISTRYINDEX);  // 创建引用

lua_getref(L, ref);   // ✅ 获取引用值到栈顶
lua_setref(L, ref);   // ✅ 设置栈顶值到引用位置

luaL_unref(L, LUA_REGISTRYINDEX, ref);     // 释放引用
```

---

## 5. Table 结构

```c
typedef struct Table {
  CommonHeader;
  lu_byte flags;
  lu_byte lsizenode;
  lu_byte mode;        /* TABLE_ARRAY(1) 或 TABLE_MAP(2) */
  unsigned int asize;  /* 物理容量 */
  unsigned int loglen; /* 逻辑长度（仅 List 使用） */
  Value *array;        /* 数组部分（倒置布局） */
  Node *node;          /* 哈希部分 */
  struct Table *metatable;
  GCObject *gclist;
} Table;
```

### 5.1 List 内存布局

```
             Values                    Tags
  ----------------------------------------------
  ...  |   Value 1   |   Value 0   |len|0|1|...
  ----------------------------------------------
                     ^ t->array 指向此处

- 值：t->array[-1] = Value 0, t->array[-2] = Value 1
- 标签：getArrTag(t, 0), getArrTag(t, 1)
```

---

## 6. 新增 VM 指令

| 指令           | 说明      |
|--------------|---------|
| `OP_NEWLIST` | 创建 List |

---

## 7. 新增标准库函数

### 7.1 table 库

```c
table.push(list, value)   // 追加元素到末尾（loglen 位置）
table.pop(list)           // 移除并返回最后一个元素
table.create(n)           // 创建容量为 n 的 List
```

### 7.2 math 库适配

所有 math 函数已适配 Slot 0 Receiver，参数从索引 2 开始：

```c
math.abs(x)     // receiver 在索引 1，x 在索引 2
math.sin(x)
math.floor(x)
// ... 其他函数同理
```

### 7.3 base 库适配

```c
pcall(func, ...)   // 内部自动插入 nil receiver
xpcall(func, err, ...)  // 同上
```

---

## 8. 原生 class 机制

SPT 提供原生类定义语法，通过 `__call` 元方法实现实例化。

### 8.1 类定义语法

```spt
class Point {
  var x = 0
  var y = 0
  
  fn __init(self, x, y) {
    self.x = x
    self.y = y
  }
  
  fn move(self, dx, dy) {
    self.x = self.x + dx
    self.y = self.y + dy
  }
}
```

### 8.2 实例化

```spt
auto p = new Point(10, 20)  // 调用 __call 元方法
p.move(5, 5)                // 方法调用
```

### 8.3 编译器生成的结构

```
class Point { ... } 编译后：

1. local Point = {}
2. Point.__index = Point
3. Point.__init = function(self, x, y) ... end
4. Point.move = function(self, dx, dy) ... end
5. setmetatable(Point, { __call = function(cls, nil, ...) 
      local obj = setmetatable({}, cls)
      -- 初始化实例字段 --
      local init = obj.__init
      if init then init(obj, ...) end
      return obj
   end })
```

### 8.4 ⚠️ 构造函数参数索引

`__init` 方法是普通方法，参数从索引 2 开始：

```c
// __init(self, x, y) 的 C 层视角：
// 索引 1: self (实例对象)
// 索引 2: x
// 索引 3: y
```

但 `new Point(x, y)` 触发的 `__call` 元方法，参数从索引 3 开始：

```c
// __call 闭包的 C 层视角：
// 索引 1: Point 表 (Slot 0 Receiver)
// 索引 2: nil (SPT 插入的 receiver)
// 索引 3: x
// 索引 4: y
```

---

## 9. sptxx C++ 绑定库

sptxx 是类似 sol2 的 C++ 绑定库，已适配 SPT 的 Slot 0 Receiver 约定。

### 9.1 头文件结构

```
sptxx/
├── sptxx.hpp        // 主入口
└── sptxx/
    ├── state.hpp    // Lua 状态管理
    ├── list.hpp     // List 绑定
    ├── map.hpp      // Map 绑定
    ├── function.hpp // 函数绑定
    ├── usertype.hpp // 用户类型绑定
    ├── stack.hpp    // 栈操作
    └── error.hpp    // 异常处理
```

### 9.2 基本用法

```cpp
#include "sptxx.hpp"

int main() {
  sptxx::state lua;
  lua.open_libraries();
  
  // 注册全局函数
  lua.set_function("add", [](int a, int b) { return a + b; });
  
  // 创建 List
  auto list = lua.create_list<int>(10);
  list.set(0, 100);
  
  // 创建 Map
  auto map = lua.create_map<std::string, int>();
  
  // 注册用户类型
  auto ut = lua.new_usertype<Point>("Point");
  ut.constructor<int, int>();
  ut.set("x", &Point::x);
  ut.set("y", &Point::y);
  ut.set("move", &Point::move);
  
  // 执行代码
  lua.do_string("auto p = new Point(10, 20); p:move(5, 5);");
}
```

### 9.3 ⚠️ 参数索引约定

sptxx 内部已正确处理参数索引：

| 场景              | 使用的函数                 | 参数起始索引 |
|-----------------|-----------------------|--------|
| 全局函数            | `extract_args_from_2` | 索引 2   |
| 方法调用            | `extract_args_from_2` | 索引 2   |
| 构造函数 (`__call`) | `extract_args_from_3` | 索引 3   |

### 9.4 List/Map 模板

```cpp
// List 模板
sptxx::list<int> int_list;      // 整数 List
sptxx::list<std::string> str_list;  // 字符串 List
sptxx::list<void> void_list;    // 无类型 List

// Map 模板
sptxx::map<std::string, int> str_int_map;
sptxx::map<void> void_map;      // 无类型 Map
```

### 9.5 用户类型绑定

```cpp
struct Warrior {
  std::string name;
  int hp;
  
  Warrior(const std::string& n, int h) : name(n), hp(h) {}
  
  void take_damage(int amount) { hp -= amount; }
};

// 绑定
auto ut = lua.new_usertype<Warrior>("Warrior");
ut.constructor<std::string, int>();  // 构造函数
ut.set("name", &Warrior::name);       // 成员变量
ut.set("hp", &Warrior::hp);
ut.set("take_damage", &Warrior::take_damage);  // 成员方法
```

---

## 10. 快速参考

### 10.1 参数索引速查表

```
普通函数:     luaL_check*(L, 2)   // 索引 2 开始
方法调用:     luaL_check*(L, 2)   // 索引 2 开始
__call:      luaL_check*(L, 3)   // 索引 3 开始 ⚠️
```

### 10.2 List API 速查表

```
创建:     lua_createarray(L, cap)
长度:     lua_arraylen(L, idx)           // loglen
容量:     lua_arraycapacity(L, idx)      // asize
设置长度: lua_arraysetlen(L, idx, newlen)
调整容量: lua_arrayresize(L, idx, newcap)
判断类型: ttisarray(o), lua_ismap(L, idx), lua_gettablemode(L, idx)
范围操作: lua_getarrayrange(), lua_setarrayrange()
移动:     lua_movearray()
迭代:     lua_nextarray(L, idx, &cursor)
其他:     lua_arrayisempty(), lua_arrayreserve()
```

### 10.3 Registry 速查表

```
创建:  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
获取:  lua_getref(L, ref);   // 值压入栈顶
设置:  lua_setref(L, ref);   // 栈顶值存入引用
释放:  luaL_unref(L, LUA_REGISTRYINDEX, ref);
```

---

## 11. 常见错误

### 11.1 参数索引错误

```c
// ❌ 错误：普通函数从索引 1 开始
int arg = luaL_checkinteger(L, 1);

// ✅ 正确：普通函数从索引 2 开始
int arg = luaL_checkinteger(L, 2);
```

### 11.2 __call 参数索引错误

```c
// ❌ 错误：__call 从索引 2 开始
int arg = luaL_checkinteger(L, 2);

// ✅ 正确：__call 从索引 3 开始
int arg = luaL_checkinteger(L, 3);
```

### 11.3 Registry API 错误

```c
// ❌ 错误：使用旧 API
lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

// ✅ 正确：使用新 API
lua_getref(L, ref);
```

### 11.4 List 索引错误

```c
// ❌ 错误：1-based 索引
arr[1]  // 这访问的是第二个元素，不是第一个！

// ✅ 正确：0-based 索引
arr[0]  // 第一个元素
```

---

## 版本信息

- **Lua 版本**: 5.5 (2026 官方版本修改)
- **SPT 版本**: 1.0
- **更新日期**: 2026-03-15

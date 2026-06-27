# SPT Lua 5.5 技术规范

> 本文档为SPT Lua 5.5 的关键技术规范，重点标注与标准 Lua 的差异和易错点。
> 本机编译测试

```
C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe -noe -c "&{Import-Module 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'; Enter-VsDevShell bc8a024f; cd C:\Users\ftp\Desktop\spt\build; cmake --build . -j 16}"
```

C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe -noe -c "&{Import-Module 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'; Enter-VsDevShell bc8a024f; cd C:\Users\ftp\Desktop\spt\spt-lsp\build;cmake .. -G "Visual Studio 17 2022" -A x64 cmake --build . -j 16}"
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
| 变量作用域       | 默认全局，`local` 声明局部          | **默认局部，`global` 声明全局**    |

---

## 2. 变量作用域与 `global` 关键字

### 2.1 基本规则

**SPT 默认变量为局部作用域，需使用 `global` 关键字显式声明全局变量。**

```spt
int x = 100;           // 局部变量（仅当前作用域可见）
global int y = 200;    // 全局变量（跨 do_string 持久化）

int add(int a, int b) { return a + b; }           // 局部函数
global int mul(int a, int b) { return a * b; }     // 全局函数
```

### 2.2 与标准 Lua 对比

| 声明方式                 | 标准 Lua | SPT Lua  |
|----------------------|--------|----------|
| `x = 100`（无类型声明）   | 全局变量   | **赋值语句**（对已声明变量合法；对未声明变量触发编译错误） |
| `local x = 100`      | 局部变量   | **语法错误**（`local` 非关键字） |
| `int x = 100`        | 不支持    | 局部变量     |
| `global int x = 100` | 不支持    | 全局变量     |

### 2.3 全局变量持久化

全局变量在同一个 `lua_State` 的多次 `do_string` 调用之间**持久化**：

```cpp
sptxx::state lua;
lua.open_libraries();

lua.do_string("global int counter = 0;");
lua.do_string("counter = counter + 1;");
lua.do_string("print(counter);");  // 输出: 1

int val = lua.get_global<int>("counter");  // val = 1
```

### 2.4 ⚠️ 易错点

```spt
// ❌ 错误：忘记 global，变量无法跨作用域访问
int x = 100;
// 在另一个 do_string 中访问 x 会得到 nil

// ✅ 正确：使用 global 声明全局变量
global int x = 100;
// 现在可以在其他 do_string 中访问 x

// ✅ 全局函数声明后可以立即在同一 do_string 中使用
global int add(int a, int b) { return a + b; }
print(add(1, 2));  // 输出: 3
```

### 2.5 设计理念

SPT 采用显式 `global` 的设计，避免了标准 Lua 中常见的"忘记 `local` 导致全局污染"问题：

- **默认安全**：变量默认局部，不会意外污染全局命名空间
- **显式意图**：`global` 关键字明确表达变量的全局意图
- **代码可读性**：一眼就能区分局部变量和全局变量

---

## 3. Slot 0 Receiver 调用约定（最重要）

### 3.1 基本规则

**所有函数调用，索引 1 永远是 Receiver，实际参数从索引 2 开始。**

```
栈布局：
  [1] receiver (Slot 0) - 始终存在
  [2] arg1              - 第一个实际参数
  [3] arg2              - 第二个实际参数
```

### 3.2 不同调用场景

| 调用方式                   | 栈布局                  | 参数起始索引      |
|------------------------|----------------------|-------------|
| `func(a, b)`           | `[nil, a, b]`        | **索引 2**    |
| `obj:method(a)`        | `[obj, a]`           | **索引 2**    |
| `obj.method(a)`        | `[obj, a]`           | **索引 2**    |
| `obj[key](a)`          | `[obj, a]`           | **索引 2**    |
| `Table(a, b)` (__call) | `[Table, a, b]`      | **索引 2**    |
| `Class(a, b)` (实例化)   | `[Class, a, b]`      | **索引 2**    |

### 3.3 C 函数模板

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
  /* 索引 1: 表本身（被 tryfuncTM 覆盖为 Receiver） */
  const char *name = luaL_checkstring(L, 2);  /* 从索引 2 开始 */
  int val = luaL_checkinteger(L, 3);
  ...
}
```

### 3.4 __call 参数索引说明

`__call` 元方法中，**参数从索引 2 开始**，与普通函数一致。

原理：Lua VM 触发 `__call` 时，tryfuncTM 用原始对象（表/类）覆盖 Slot 0 的
Receiver，不移动栈、不增长栈。因此：

- 索引 1 = 表/类本身（作为 Receiver / self）
- 索引 2+ = 实际参数

---

## 4. List 与 Map 分离

### 4.1 类型判断

```c
#define ttisarray(o)  // 判断是否为 List
#define ttistable(o)  // 判断是否为 Map
```

### 4.2 行为差异

| 操作   | List (`LUA_VARRAY`)       | Map (`LUA_VTABLE`)         |
|------|---------------------------|----------------------------|
| 创建   | `lua_createarray(L, cap)` | `lua_createtable(L, 0, 0)` |
| 索引   | 仅整数，**0-based**           | 任意类型                       |
| `#t` | 返回 `loglen`               | **返回 0**                   |
| 越界访问 | **抛出错误**                  | 返回 nil                     |
| 越界写入 | 写 `loglen` 位置触发**追加**；其他越界**抛出错误** | 正常插入                       |

### 4.3 List 边界检查

```c
/* 以下操作会抛出错误 */
arr[-1]      // 负数索引
arr[100]     // 越界（>= loglen）
arr[1.5]     // 浮点索引
```

### 4.4 List API

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

## 5. Registry 引用机制（破坏性变更）

### 5.1 ⚠️ 禁止使用的 API

```c
// ❌ 禁止！会导致未定义行为
lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
lua_rawseti(L, LUA_REGISTRYINDEX, ref);
```

### 5.2 正确用法

```c
int ref = luaL_ref(L, LUA_REGISTRYINDEX);  // 创建引用

lua_getref(L, ref);   // ✅ 获取引用值到栈顶
lua_setref(L, ref);   // ✅ 设置栈顶值到引用位置

luaL_unref(L, LUA_REGISTRYINDEX, ref);     // 释放引用
```

---

## 6. Table 结构

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

### 6.1 List 内存布局

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

## 7. 新增 VM 指令

| 指令           | 说明      |
|--------------|---------|
| `OP_NEWLIST` | 创建 List |

---

## 8. 新增标准库函数

### 8.1 table 库

```c
table.push(list, value)   // 追加元素到末尾（loglen 位置）
table.pop(list)           // 移除并返回最后一个元素
table.create(narray, nhash)  // 创建表（注意：创建的是 Map，非 List）
```

> **注意**：`table.create` 内部调用 `lua_createtable`，始终创建 **Map**
> （`t->mode = TABLE_MAP`）。若需创建 List，请使用 `lua_createarray(L, cap)`
> 或 SPT 语法 `[]` / `[a, b, c]`。

### 8.2 math 库适配

所有 math 函数已适配 Slot 0 Receiver，参数从索引 2 开始：

```c
math.abs(x)     // receiver 在索引 1，x 在索引 2
math.sin(x)
math.floor(x)
// ... 其他函数同理
```

### 8.3 base 库适配

```c
pcall(func, ...)   // 内部自动插入 nil receiver
xpcall(func, err, ...)  // 同上
```

---

## 9. 原生 class 机制

SPT 提供原生类定义语法，通过 `__call` 元方法实现实例化。

### 9.1 类定义语法

```spt
class Point {
  int x = 0;          // 实例字段（带类型注解，可初始化）
  int y = 0;

  void __init(int x, int y) {   // 构造函数（this 隐式，无需显式声明）
    this.x = x;
    this.y = y;
  }

  void move(int dx, int dy) {   // 实例方法（this 隐式）
    this.x = this.x + dx;
    this.y = this.y + dy;
  }
}
```

> **注意**：类方法的 `this` 是**隐式**的 Slot 0 Receiver，无需在参数列表中
> 显式声明。方法体内通过 `this` 访问实例。字段声明必须带类型注解
> （如 `int x = 0`），不支持 `var`/`fn` 关键字。方法语法为
> `type name(params) { body }`，如 `void __init(int x, int y) { ... }`。

### 9.2 实例化

```spt
auto p = Point(10, 20)   // 调用 __call 元方法
p:move(5, 5)             // 方法调用
```

> SPT 标准实例化语法是 `ClassName(...)`，直接触发 class 表的 `__call`
> 元方法。`new` 关键字不在 g4 文法中。

### 9.3 编译器生成的结构

```
class Point { ... } 编译后：

1. local Point = {}
2. Point.__index = Point
3. Point.__init = function(this, x, y) ... end   // this 隐式注入
4. Point.move = function(this, dx, dy) ... end    // this 隐式注入
5. setmetatable(Point, { __call = function(cls, ...)
      local obj = setmetatable({}, cls)
      -- 初始化实例字段 --
      local init = obj.__init
      if init then init(obj, ...) end
      return obj
   end })
```

`__call` 闭包仅 1 个固定参数 `cls`（Slot 0），构造参数通过隐藏 varargs
转发给 `__init`。

### 9.4 构造函数参数索引

`__init` 方法是普通方法，参数从索引 2 开始：

```c
// __init(this, x, y) 的 C 层视角：
// 索引 1: this (实例对象)
// 索引 2: x
// 索引 3: y
```

`Point(x, y)` 触发的 `__call` 元方法同样从索引 2 开始（tryfuncTM 已将
cls 覆盖到 Slot 0）：

```c
// __call 闭包的 C 层视角：
// 索引 1: Point 表 (cls, 作为 Receiver)
// 索引 2: x
// 索引 3: y
```

---

## 10. sptxx C++ 绑定库

sptxx 是类似 sol2 的 C++ 绑定库，已适配 SPT 的 Slot 0 Receiver 约定。

### 10.1 头文件结构

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
    ├── coroutine.hpp // 协程绑定
    └── error.hpp    // 异常处理
```

### 10.2 基本用法

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
  lua.do_string("auto p = Point(10, 20); p:move(5, 5);");
}
```

### 10.3 参数索引约定

sptxx 内部已正确处理参数索引：

| 场景              | 使用的函数                 | 参数起始索引 |
|-----------------|-----------------------|--------|
| 全局函数            | `extract_args_from_2` | 索引 2   |
| 方法调用            | `extract_args_from_2` | 索引 2   |
| 构造函数 (`__call`) | `extract_args_from_2` | 索引 2   |

### 10.4 List/Map 模板

```cpp
// List 模板
sptxx::list<int> int_list;      // 整数 List
sptxx::list<std::string> str_list;  // 字符串 List
sptxx::list<void> void_list;    // 无类型 List

// Map 模板
sptxx::map<std::string, int> str_int_map;
sptxx::map<void> void_map;      // 无类型 Map
```

### 10.5 用户类型绑定

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

## 11. 快速参考

### 11.1 参数索引速查表

```
普通函数:     luaL_check*(L, 2)   // 索引 2 开始
方法调用:     luaL_check*(L, 2)   // 索引 2 开始
__call:      luaL_check*(L, 2)   // 索引 2 开始
```

### 11.2 List API 速查表

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

### 11.3 Registry 速查表

```
创建:  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
获取:  lua_getref(L, ref);   // 值压入栈顶
设置:  lua_setref(L, ref);   // 栈顶值存入引用
释放:  luaL_unref(L, LUA_REGISTRYINDEX, ref);
```

---

## 12. 常见错误

### 12.1 参数索引错误

```c
// ❌ 错误：普通函数从索引 1 开始
int arg = luaL_checkinteger(L, 1);

// ✅ 正确：普通函数从索引 2 开始
int arg = luaL_checkinteger(L, 2);
```

### 12.2 __call 参数索引错误

```c
// ❌ 错误：__call 从索引 3 开始（旧约定，已废弃）
int arg = luaL_checkinteger(L, 3);

// ✅ 正确：__call 从索引 2 开始（tryfuncTM 覆盖 receiver 后）
int arg = luaL_checkinteger(L, 2);
```

### 12.3 Registry API 错误

```c
// ❌ 错误：使用旧 API
lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

// ✅ 正确：使用新 API
lua_getref(L, ref);
```

### 12.4 List 索引错误

```c
// ❌ 错误：1-based 索引
arr[1]  // 这访问的是第二个元素，不是第一个！

// ✅ 正确：0-based 索引
arr[0]  // 第一个元素
```

---

## 13. 外部符号声明 `declare`（编译期擦除）

`declare` 用于声明「存在但实现在别处」的符号 —— 典型是 **C 绑定的外部库**
（如 SDL）、或运行时由宿主注册的全局。它在编译期被**完全擦除**：不产生任何
字节码、不创建任何绑定，仅供类型检查与 LSP 消费（跳转 / hover / 补全）。

> 设计与 SPT「类型 / `const` 是提示、运行期无效」的既有哲学一致 —— `declare`
> 是这套「提示即擦除」的极致形态。

### 13.1 两种形式

```spt
// 形式一：模块声明块 —— 描述外部模块的导出形状。
// 复用 import 的 `from "..."`，与 import 一一对应。
declare from "sdl" {
    int Init(int flags);
    Window CreateWindow(str title, int x, int y, int w, int h, int flags);
    const int INIT_VIDEO;
    void video.SetMode(int w, int h);     // dotted 名 -> 子命名空间
    vars GetVersion();                    // 多返回函数签名
    class Window {                        // 外部类型的字段/方法签名
        int w;
        void SetTitle(str title);
        static Window Create(str title, int w, int h);
    }
}

// 形式二：环境声明 —— 顶层单个外部符号（宿主注册为全局时）。
declare int SDL_Init(int flags);
declare const int SDL_INIT_VIDEO;
```

业务代码照常书写，工具全程可解析符号；运行期 `import ... from "sdl"` 仍走真实
`require`，由 C 侧的真实绑定接管：

```spt
import { Init, CreateWindow, INIT_VIDEO } from "sdl";
int rc = Init(INIT_VIDEO);                 // 跳转到声明处；hover 显示签名+描述
auto win = CreateWindow("hi", 0, 0, 800, 600, 0);
win:SetTitle("ready");                     // 补全列出 Window 的方法
```

### 13.2 描述：文档注释 `///` 与 `/** ... */`

紧邻声明之前的文档注释作为该符号的**描述**，挂到 AST 节点上供工具消费；对
编译 / 运行完全透明。多行 `///` 会累积。

```spt
/// 数学库（宿主以 C 实现，此处仅声明子集形状）。
declare from "math" {
    /// 向下取整。
    float floor(float x);
    /** 绝对值。 */
    int abs(int x);
}
```

> 普通注释 `// ...` 与 `/* ... */` 仍照常跳过，不作为描述；`/**/` 是空块注释，
> 也不是文档注释。

### 13.3 约束

| 规则                       | 说明                                              |
|--------------------------|-------------------------------------------------|
| 不允许 `auto`              | 声明必须给出确切类型（无初始化器可推断）。`declare auto x;` 报错 |
| 不允许初始化器              | `declare int x = 5;` 报错                          |
| 不允许函数 / 方法体          | 体必须替换为 `;`。`declare int f(){...}` 报错          |
| 编译期擦除                  | 声明的符号若无宿主提供，运行期为 `null`（不产生任何绑定）       |
| 与真实定义共存              | 声明不替换、不破坏同名真实绑定（如 `import ... from "math"`） |

---

## 14. 模块系统（import / export）

SPT 采用类 ES6 的 `import`/`export` 语法，底层编译为 Lua `require` 调用，完全复用 Lua 模块缓存机制。

### 14.1 import 语法

**命名空间导入**（整个模块表）：
```spt
import * as m from "math";
m.abs(-42);  // 42
```

**具名导入**（支持 `as` 别名）：
```spt
import { abs, max as big } from "math";
abs(-10);  // 10
big(3, 5);  // 5
```

**不支持**：默认导入 `import x from "path"`、副作用导入 `import "path"`、动态 `import("path")`。

### 14.2 export 语法

`export` 作为声明前缀，仅顶层（`scope_depth==0`）有效：

```spt
export int val = 42;              // 导出变量
export int square(int x) { ... }  // 导出函数
export class Point { ... }        // 导出类
```

**不支持**：`export { a, b }`、`export default`、`export * from "path"`、`export vars a,b=...`（多变量 export 语法合法但不生效）。

### 14.3 模块路径解析

路径是**模块名**（不含扩展名），搜索顺序：

1. 主脚本所在目录：`script_dir/?.spt`
2. 环境变量 `SPT_PATH`（分号分隔）
3. 当前工作目录：`./?.spt`

```spt
import * as utils from "utils";  // 查找 utils.spt
```

**可透明导入 Lua 内置库**：`import { abs } from "math"` 会先走 Lua 标准 searcher，返回 math 库。

**不支持**：相对路径 `./xxx`、绝对路径、点分路径 `a.b.c`（会查找 `a.b.c.spt` 而非 `a/b/c.spt`）。

### 14.4 加载时机与缓存

- **运行时加载**：import 编译为 `require("path")`，执行到该语句才加载
- **复用 `package.loaded` 缓存**：首次加载执行模块体，后续直接返回缓存
- **循环依赖**：无保护，行为同 Lua（被依赖方拿到 `true` 而非 exports 表，访问成员报运行时错误）

### 14.5 exports 表构造

模块体执行完毕后，编译器自动：
1. 收集所有 `is_module_root && is_exported` 的声明（变量/函数/类）
2. 构造 exports 表 `{ name1 = val1, name2 = val2, ... }`
3. `return` 该表

无导出的模块不 return，`require` 返回 `true`——命名空间导入后访问成员会报错。

### 14.6 命名空间 vs 具名导入的语义差异

| 维度 | `import * as m` | `import { a }` |
|------|-----------------|----------------|
| 绑定 | 整个 exports 表 | 每个名字独立局部变量 |
| 访问 | `m.member` 动态索引 | 直接变量引用 |
| 模块表后续变更 | 可见（持表引用） | 不可见（值已拷贝） |
| 无导出模块 | `m=true`，访问报错 | `__tmp.a` 返回 `nil`，不报错 |

### 14.7 与标准 Lua 对比

| 特性 | 标准 Lua | SPT |
|------|----------|-----|
| 导入 | `local m = require("path")` | `import * as m from "path"` |
| 导出 | 手动 `return { a = a }` | 声明前加 `export`，自动收集 |
| 别名 | 无语法，手动赋值 | `import { a as b }` |
| 路径 | `package.path`，支持 `.` 分隔 | `?.spt` 模板，不支持 `.` 分隔 |
| 内置库 | `require("math")` | `import * as math from "math"` 透明支持 |

---

## 版本信息

- **Lua 版本**: 5.5 (2026 官方版本修改)
- **SPT 版本**: 1.0
- **更新日期**: 2026-06-18

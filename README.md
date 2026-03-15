# SPT Lua 5.5 修改指南

> 本文档详细记录了 SPT 项目对 Lua 5.5 的所有修改，旨在为开发脚本绑定的 C API 和 CXX API（类似 sol2）提供参考。

---

## 目录

1. [概述](#1-概述)
2. [核心设计原则](#2-核心设计原则)
3. [类型系统修改](#3-类型系统修改)
4. [Table 结构改造](#4-table-结构改造)
5. [List 与 Map 拆分](#5-list-与-map-拆分)
6. [Slot 0 Receiver 调用约定](#6-slot-0-receiver-调用约定)
7. [新增 C API](#7-新增-c-api)
8. [VM 指令修改](#8-vm-指令修改)
9. [标准库适配](#9-标准库适配)
10. [边界检查与错误处理](#10-边界检查与错误处理)
11. [CXX API 开发指南](#11-cxx-api-开发指南)
12. [附录：关键宏和函数](#12-附录关键宏和函数)

---

## 1. 概述

### 1.1 修改背景

SPT 项目基于 2026 年发布的官方 Lua 5.5 进行深度定制，主要目标是：

- **统一调用约定**：所有函数强制 Slot 0 为 Receiver，支持面向对象编程范式
- **List/Map 语义分离**：彻底拆分数组和哈希表的语义，提供更直观的数据结构
- **性能优化**：List 长度与容量分离，支持 O(1) 摊销追加操作
- **类型安全**：严格的边界检查和类型验证

### 1.2 文件清单

| 文件           | 修改内容                |
|--------------|---------------------|
| `lobject.h`  | 新增类型标签、Table 结构改造   |
| `ltable.h`   | 新增数组操作宏、模式枚举        |
| `ltable.c`   | List/Map 核心实现       |
| `lapi.c`     | 新增数组 C API          |
| `lvm.c`      | VM 指令实现、边界检查        |
| `lopcodes.h` | 新增 OP_NEWLIST 指令    |
| `ltablib.c`  | table.push/pop 等库函数 |
| `lmathlib.c` | Slot 0 Receiver 适配  |
| `lbaselib.c` | Slot 0 Receiver 适配  |
| `ldo.c`      | 调用栈帧处理              |
| `ldblib.c`   | Hook 回调 Receiver 垫片 |

---

## 2. 核心设计原则

### 2.1 统一 Receiver 调用约定

**原则**：所有函数调用时，Slot 0（栈索引 1）强制预留为 Receiver。

```
栈布局：
  [1] receiver (Slot 0) - 始终存在
  [2] arg1    (Slot 1) - 第一个实际参数
  [3] arg2    (Slot 2) - 第二个实际参数
  ...
```

**示例**：

```c
/* C 函数签名约定 */
static int my_function(lua_State *L) {
  /* receiver 在索引 1，但通常被忽略（为 nil）*/
  /* 实际参数从索引 2 开始 */
  int arg1 = luaL_checkinteger(L, 2);
  int arg2 = luaL_checkinteger(L, 3);
  ...
}
```

### 2.2 List/Map 语义分离

| 特性      | List (TABLE_ARRAY) | Map (TABLE_MAP) |
|---------|--------------------|-----------------|
| 类型标签    | `LUA_VARRAY`       | `LUA_VTABLE`    |
| 键类型     | 仅整数 (0-based)      | 任意类型            |
| `#` 运算符 | 返回 `loglen`        | 固定返回 0          |
| 内存布局    | 倒置数组（值 + 标签分离）     | 标准哈希表           |
| 边界检查    | 严格拦截越界             | 无限制             |

---

## 3. 类型系统修改

### 3.1 新增类型标签

```c
/* lobject.h */

/* 基础类型 */
#define LUA_TARRAY (LUA_NUMTYPES)      /* 数组类型 */

/* 变体类型 */
#define LUA_VARRAY makevariant(LUA_TARRAY, 0)  /* 数组变体 */

/* 类型判断宏 */
#define ttisarray(o) checktag((o), ctb(LUA_VARRAY))
#define ttistable(o) checktag((o), ctb(LUA_VTABLE))
```

### 3.2 类型转换宏

```c
/* lobject.h */

/* 获取数组值 */
#define avalue(o) check_exp(ttisarray(o), gco2t(val_(o).gc))

/* 设置数组值到栈 */
#define setavalue(L, obj, x) \
  { TValue *io = (obj); \
    Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); \
    settt_(io, ctb(LUA_VARRAY)); \
    checkliveness(L, io); }

#define setavalue2s(L, o, a) setavalue(L, s2v(o), a)
```

### 3.3 类型名称

```c
/* lapi.c */
LUA_API const char *lua_typename(lua_State *L, int t) {
  ...
  if (t == LUA_TARRAY)
    return "array";
  return ttypename(t);
}
```

---

## 4. Table 结构改造

### 4.1 Table 结构定义

```c
/* lobject.h */
typedef struct Table {
  CommonHeader;
  lu_byte flags;       /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;   /* log2 of number of slots of 'node' array */
  lu_byte mode;        /* TABLE_ARRAY (1) or TABLE_MAP (2) */
  unsigned int asize;  /* number of slots in 'array' array (physical capacity) */
  unsigned int loglen; /* logical length, only used by TABLE_ARRAY */
  Value *array;        /* array part (inverted layout) */
  Node *node;          /* hash part */
  struct Table *metatable;
  GCObject *gclist;
} Table;
```

### 4.2 模式枚举

```c
/* lobject.h */
#define TABLE_ARRAY 1  /* 纯数组模式（List） */
#define TABLE_MAP 2    /* 纯哈希模式（Map） */
```

### 4.3 数组内存布局

```
             Values                              Tags
  --------------------------------------------------------
  ...  |   Value 1     |   Value 0     |unsigned|0|1|...
  --------------------------------------------------------
                                       ^ t->array

- `t->array` 指向两个数组之间
- 值通过负索引访问：`t->array[-1]` = Value 0
- 标签通过正索引访问：`getArrTag(t, 0)` = Tag 0
- `unsigned` 用于存储长度提示（lenhint）
```

---

## 5. List 与 Map 拆分

### 5.1 List 特性

| 特性    | 描述                      |
|-------|-------------------------|
| 索引    | 0-based                 |
| 容量    | `asize` 物理容量            |
| 长度    | `loglen` 逻辑长度           |
| 扩容    | 平摊扩容（1.5 倍增长）           |
| 缩容    | 仅缩减 `loglen`，不清理物理内存    |
| GC 安全 | 缩容时清理截断槽位为 `LUA_VEMPTY` |

### 5.2 Map 特性

| 特性      | 描述                      |
|---------|-------------------------|
| 键类型     | 任意类型（整数、字符串、表等）         |
| `#` 运算符 | 固定返回 0                  |
| 遍历      | 使用 `next()` 或 `pairs()` |
| 边界检查    | 无                       |

### 5.3 长度运算符 (#) 实现

```c
/* ltable.c - luaH_getn */
lua_Unsigned luaH_getn(lua_State *L, Table *t) {
  /* LIST: return logical length */
  if (t->mode == TABLE_ARRAY)
    return t->loglen;
  
  /* MAP: always return 0 according to design document */
  if (t->mode == TABLE_MAP)
    return 0;
  
  /* regular table logic (not used in SPT) */
  ...
}

/* lvm.c - OP_LEN */
void luaV_objlen(lua_State *L, StkId ra, const TValue *rb) {
  ...
  case LUA_VARRAY: {
    Table *h = avalue(rb);
    tm = fasttm(L, h->metatable, TM_LEN);
    if (tm) break;
    setivalue(s2v(ra), (lua_Integer)h->loglen);
    return;
  }
  case LUA_VTABLE: {
    Table *h = hvalue(rb);
    tm = fasttm(L, h->metatable, TM_LEN);
    if (tm) break;
    setivalue(s2v(ra), l_castU2S(luaH_getn(L, h)));
    return;
  }
  ...
}
```

---

## 6. Slot 0 Receiver 调用约定

### 6.1 调用栈布局

```
CallInfo 栈帧：
  ci->func.p     -> [function]
  ci->func.p + 1 -> [receiver] (Slot 0, 通常为 nil)
  ci->func.p + 2 -> [arg1]     (Slot 1)
  ci->func.p + 3 -> [arg2]     (Slot 2)
  ...
```

### 6.2 C 函数参数提取模式

```c
/* 标准模式 */
static int my_func(lua_State *L) {
  /* 索引 1: receiver (通常为 nil) */
  /* 索引 2+: 实际参数 */
  
  int arg1 = luaL_checkinteger(L, 2);
  int arg2 = luaL_checkinteger(L, 3);
  const char *str = luaL_checkstring(L, 4);
  
  /* 返回值压入栈顶 */
  lua_pushinteger(L, result);
  return 1; /* 返回值数量 */
}
```

### 6.3 方法调用语义

```lua
-- 点调用：receiver 是对象本身
obj:method(arg1, arg2)
-- 栈布局：[nil, obj, arg1, arg2]

-- 索引调用：receiver 是容器
arr[0] = value
-- 栈布局：[nil, arr, 0, value]

-- 函数调用：receiver 为 nil
func(arg1, arg2)
-- 栈布局：[nil, arg1, arg2]
```

### 6.4 闭包 this 穿透

| 函数类型    | Receiver 命名    | 说明     |
|---------|----------------|--------|
| 类方法     | `"self"`       | 面向对象方法 |
| 普通函数/闭包 | `"(receiver)"` | 穿透调用   |

### 6.5 C 层回调垫片

```c
/* ltablib.c - sort_comp */
static int sort_comp(lua_State *L, int a, int b) {
  if (lua_isnil(L, 3)) {
    return lua_compare(L, a, b, LUA_OPLT);
  } else {
    lua_pushvalue(L, 3);    /* push function */
    lua_pushnil(L);         /* push 隐式的 receiver */
    lua_pushvalue(L, a - 2); /* 补偿 func 和 nil */
    lua_pushvalue(L, b - 3); /* 补偿 func, nil 和 a */
    lua_call(L, 3, 1);      /* 3 个参数：receiver, a, b */
    ...
  }
}

/* ldblib.c - hook 回调 */
lua_pushnil(L);  /* 为 errhandler 垫入 nil receiver */
lua_call(L, 3, 1); /* receiver + 2 个参数 */
```

---

## 7. 新增 C API

### 7.1 基础数组 API

#### lua_createarray

```c
/* lapi.c */
LUA_API void lua_createarray(lua_State *L, int narray);
/*
 * 创建一个新的数组（List）
 * @param L Lua 状态
 * @param narray 初始容量（逻辑长度初始为 0）
 * @return 新数组压入栈顶
 */
```

#### lua_arrayresize

```c
LUA_API void lua_arrayresize(lua_State *L, int idx, lua_Integer newsize);
/*
 * 调整数组的物理容量
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @param newsize 新容量（不能小于当前逻辑长度）
 */
```

#### lua_arraylen

```c
LUA_API lua_Integer lua_arraylen(lua_State *L, int idx);
/*
 * 获取数组的逻辑长度
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @return 逻辑长度
 */
```

#### lua_arraysetlen

```c
LUA_API void lua_arraysetlen(lua_State *L, int idx, lua_Integer newlen);
/*
 * 设置数组的逻辑长度
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @param newlen 新长度（必须在 [0, asize] 范围内）
 * 
 * 注意：缩容时会清理截断的槽位为 LUA_VEMPTY，保证 GC 安全
 */
```

### 7.2 扩展数组 API

#### lua_arraycapacity

```c
LUA_API lua_Integer lua_arraycapacity(lua_State *L, int idx);
/*
 * 获取数组的物理容量（asize）
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @return 物理容量
 */
```

#### lua_gettablemode

```c
LUA_API int lua_gettablemode(lua_State *L, int idx);
/*
 * 获取表的模式
 * @param L Lua 状态
 * @param idx 栈索引
 * @return TABLE_ARRAY(1) 为数组，TABLE_MAP(2) 为映射，0 为无效类型
 */
```

#### lua_ismap

```c
LUA_API int lua_ismap(lua_State *L, int idx);
/*
 * 检查对象是否为 Map（TABLE_MAP 模式）
 * @param L Lua 状态
 * @param idx 栈索引
 * @return 1 如果是 Map，0 否则
 */
```

#### lua_getarrayrange

```c
LUA_API void lua_getarrayrange(lua_State *L, int idx, lua_Integer start, lua_Integer end);
/*
 * 获取数组范围的元素
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @param start 起始索引（包含）
 * @param end 结束索引（不包含）
 * 
 * 将元素从索引 start 到 end-1 压入栈顶
 *
 */
```

#### lua_setarrayrange

```c
LUA_API void lua_setarrayrange(lua_State *L, int idx, lua_Integer start, lua_Integer count);
/*
 * 从栈设置数组范围的元素
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @param start 起始索引
 * @param count 元素数量（从栈顶获取）
 * 
 * 从栈顶弹出 count 个值并设置到数组中
 *
 */
```

#### lua_movearray

```c
LUA_API void lua_movearray(lua_State *L, int fromidx, int toidx, lua_Integer from, lua_Integer to,
                           lua_Integer count);
/*
 * 移动数组元素
 * @param L Lua 状态
 * @param fromidx 源数组索引
 * @param toidx 目标数组索引
 * @param from 源起始位置
 * @param to 目标起始位置
 * @param count 元素数量
 *
 */
```

#### lua_nextarray

```c
LUA_API int lua_nextarray(lua_State *L, int idx, lua_Integer *cursor);
/*
 * 迭代数组元素
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @param cursor 迭代器游标（初始为 -1）
 * @return 1 如果还有元素，0 如果迭代完成
 * 
 * 每次调用将 (index, value) 压入栈顶
 * 使用示例:
 *   lua_Integer cursor = -1;
 *   while (lua_nextarray(L, idx, &cursor)) {
 *     // 栈顶：value, 栈顶 -1: index
 *     lua_pop(L, 2);
 *   }
 *
 */
```

#### lua_arrayisempty

```c
LUA_API int lua_arrayisempty(lua_State *L, int idx);
/*
 * 检查数组是否为空（loglen == 0）
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @return 1 如果为空，0 否则
 */
```

#### lua_arrayreserve

```c
LUA_API void lua_arrayreserve(lua_State *L, int idx, lua_Integer cap);
/*
 * 预留数组容量（只增不减）
 * @param L Lua 状态
 * @param idx 数组的栈索引
 * @param cap 要预留的容量
 */
```

### 7.3 原始长度（通用）

```c
/* lapi.c */
LUA_API lua_Unsigned lua_rawlen(lua_State *L, int idx);
/*
 * 获取对象的原始长度
 * - 数组：返回 loglen
 * - 表：返回 luaH_getn 的结果
 * - 字符串：返回字符长度
 * - userdata：返回字节长度
 */
```

---

## 8. VM 指令修改

### 8.1 OP_NEWLIST

```c
/* lopcodes.h */
OP_NEWLIST, /* A vB vC k  R[A] := [] (array) */

/* lvm.c */
vmcase(OP_NEWLIST) {
  StkId ra = RA(i);
  unsigned c = cast_uint(GETARG_vC(i)); /* array size */
  Table *t;
  if (TESTARG_k(i)) { /* non-zero extra argument? */
    lua_assert(GETARG_Ax(*pc) != 0);
    c += cast_uint(GETARG_Ax(*pc)) * (MAXARG_vC + 1);
  }
  pc++;                 /* skip extra argument */
  L->top.p = ra + 1;    /* correct top in case of emergency GC */
  t = luaH_newarray(L); /* create array */
  setavalue2s(L, ra, t);
  if (c != 0)
    luaH_resizearray(L, t, c); /* resize array part only */
  checkGC(L, ra + 1);
  vmbreak;
}
```

### 8.2 OP_GETTABLE (数组边界检查)

```c
/* lvm.c */
vmcase(OP_GETTABLE) {
  StkId ra = RA(i);
  TValue *rb = vRB(i);
  TValue *rc = vRC(i);
  lu_byte tag;

  if (ttisarray(rb)) {
    Table *t = avalue(rb);
    if (ttisinteger(rc)) {
      lua_Integer idx = ivalue(rc);
      /* 负数索引检查 */
      if (idx < 0)
        luaG_runerror(L, "list index out of range: negative index %I", (LUAI_UACINT)idx);
      /* 越界检查 */
      if (idx >= (lua_Integer)t->loglen)
        luaG_runerror(L, "list index out of range: index %I >= length %u", 
                      (LUAI_UACINT)idx, t->loglen);
      tag = luaH_getint(t, idx, s2v(ra));
    } else if (ttisnumber(rc)) {
      luaG_runerror(L, "list index must be integer, not float");
    } else {
      /* 非数字 key，放行给 finishget 触发 __index */
      tag = LUA_VEMPTY;
    }
  } else {
    /* Map: 原有逻辑 */
    ...
  }
  ...
}
```

### 8.3 OP_SETTABLE (数组边界检查)

```c
/* lvm.c */
vmcase(OP_SETTABLE) {
  StkId ra = RA(i);
  TValue *rb = vRB(i); /* key */
  TValue *rc = RKC(i); /* value */
  int hres;

  if (ttisarray(s2v(ra))) {
    Table *t = avalue(s2v(ra));
    if (ttisinteger(rb)) {
      lua_Integer idx = ivalue(rb);
      /* 负数索引检查 */
      if (idx < 0)
        luaG_runerror(L, "list index out of range: negative index %I", (LUAI_UACINT)idx);
      /* 严格越界拦截 */
      if (idx >= (lua_Integer)t->loglen)
        luaG_runerror(L, "list index out of range: index %I >= length %u", 
                      (LUAI_UACINT)idx, t->loglen);
      /* 直接写入 */
      lu_byte *tagp = getArrTag(t, cast_uint(idx));
      if (checknoTM(t->metatable, TM_NEWINDEX) || !tagisempty(*tagp)) {
        fval2arr(t, cast_uint(idx), tagp, rc);
        hres = HOK;
      } else {
        hres = ~cast_int(idx);
      }
    } else if (ttisnumber(rb)) {
      luaG_runerror(L, "list index must be integer, not float");
    } else {
      /* 非数字 key，放行给 __newindex */
      hres = HNOTFOUND;
    }
  } else {
    /* Map: 原有逻辑 */
    ...
  }
  ...
}
```

### 8.4 OP_TFORCALL

```c
/* lvm.c */
vmcase(OP_TFORCALL) {
  StkId ra = RA(i);
  setobjs2s(L, ra + 6, ra + 3); /* control (3rd arg) */
  setobjs2s(L, ra + 3, ra);     /* function */
  setobjs2s(L, ra + 4, ra);     /* receiver (1st arg) */
  setobjs2s(L, ra + 5, ra + 1); /* state (2nd arg) */
  L->top.p = ra + 3 + 4;
  ProtectNT(luaD_call(L, ra + 3, GETARG_C(i)));
  updatestack(ci);
  ...
}
```

---

## 9. 标准库适配

### 9.1 table 库

```c
/* ltablib.c */

/* 导出函数列表 */
static const luaL_Reg tab_funcs[] = {
  {"concat", tconcat},
  {"create", tcreate},
  {"insert", tinsert},
  {"pack", tpack},
  {"unpack", tunpack},
  {"remove", tremove},
  {"move", tmove},
  {"sort", sort},
  {"push", lpush},    /* 新增 */
  {"pop", lpop},      /* 新增 */
  {NULL, NULL}
};
```

#### table.push

```c
/* lpush - Append a value to the end of a list
 * Arguments: receiver (nil), list, value
 * Returns: nothing
 */
static int lpush(lua_State *L) {
  luaL_checktype(L, 2, LUA_TARRAY);
  luaL_checkany(L, 3);
  
  lua_Integer size = luaL_len(L, 2);
  if (l_unlikely(size >= LUA_MAXINTEGER))
    return luaL_error(L, "list overflow");
  
  lua_pushvalue(L, 3);
  lua_seti(L, 2, size); /* 触发 finishset 的 append 路径 */
  return 0;
}
```

#### table.pop

```c
/* lpop - Remove and return the last element from a list
 * Arguments: receiver (nil), list
 * Returns: the removed element, or nil if list is empty
 */
static int lpop(lua_State *L) {
  luaL_checktype(L, 2, LUA_TARRAY);
  
  lua_Integer size = luaL_len(L, 2);
  if (size == 0) {
    lua_pushnil(L);
    return 1;
  }
  
  lua_geti(L, 2, size - 1); /* 获取最后一个元素 */
  lua_arraysetlen(L, 2, size - 1); /* 缩容并清理槽位 */
  return 1;
}
```

### 9.2 math 库

所有 math 函数都适配了 Receiver 约定：

```c
/* lmathlib.c */
/* math_abs - receiver is arg1, x is arg2 */
static int math_abs(lua_State *L) {
  if (lua_isinteger(L, 2)) {
    lua_Integer n = lua_tointeger(L, 2);
    if (n < 0) n = (lua_Integer)(0u - (lua_Unsigned)n);
    lua_pushinteger(L, n);
  } else {
    lua_pushnumber(L, l_mathop(fabs)(luaL_checknumber(L, 2)));
  }
  return 1;
}

/* 其他函数同理：math_sin, math_cos, math_floor, math_ceil, ... */
```

### 9.3 base 库

```c
/* lbaselib.c */

/* pcall - 强制压入 nil receiver */
static int luaB_pcall(lua_State *L) {
  luaL_checkany(L, 2);
  lua_pushnil(L);  /* receiver 垫片 */
  lua_insert(L, 2);
  return lua_pcallk(L, lua_gettop(L) - 2, LUA_MULTRET, 0, 0, finishpcall);
}

/* xpcall - 强制压入 nil receiver */
static int luaB_xpcall(lua_State *L) {
  luaL_checkany(L, 2);
  lua_pushnil(L);  /* receiver 垫片 */
  lua_insert(L, 2);
  status = lua_pcallk(L, n - 2, LUA_MULTRET, 3, 2, finishpcall);
  ...
}
```

---

## 10. 边界检查与错误处理

### 10.1 List 边界检查规则

| 情况          | 错误信息                                               |
|-------------|----------------------------------------------------|
| 负数索引        | `"list index out of range: negative index %I"`     |
| 浮点索引        | `"list index must be integer, not float"`          |
| `>= loglen` | `"list index out of range: index %I >= length %u"` |
| 非整数 key     | 放行给 `__index`/`__newindex` 元方法                     |

### 10.2 错误处理中的 Receiver 垫片

```c
/* ldblib.c - 错误处理 */
/* === SPT 专属修改：为 errhandler 垫入 nil receiver === */
/* 栈结构从 [err_msg] 调整为 [errfunc, nil_receiver, err_msg] */

setobjs2s(L, L->top.p - 1, errfunc);  /* 在 err_msg 原位置放入 errfunc */
L->top.p += 2;                         /* 栈高度增加 2 (函数 + receiver) */

/* 调用 errfunc，参数：receiver(nil) + err_msg */
luaD_callnoyield(L, L->top.p - 3, 1);
```

### 10.3 GC 安全清理

```c
/* lapi.c - lua_arraysetlen */
LUA_API void lua_arraysetlen(lua_State *L, int idx, lua_Integer newlen) {
  ...
  /* 缩容时清理截断槽位（GC 安全） */
  {
    unsigned i;
    for (i = cast_uint(newlen); i < t->loglen; i++)
      *getArrTag(t, i) = LUA_VEMPTY;
  }
  t->loglen = cast_uint(newlen);
  ...
}
```

---

## 11. 附录：关键宏和函数

### 11.1 类型判断宏

```c
/* lobject.h */
#define ttisarray(o) checktag((o), ctb(LUA_VARRAY))
#define ttistable(o) checktag((o), ctb(LUA_VTABLE))
#define ttisnumber(o) checktype((o), LUA_TNUMBER)
#define ttisinteger(o) checktag((o), LUA_VNUMINT)
#define ttisfloat(o) checktag((o), LUA_VNUMFLT)
#define ttisstring(o) checktype((o), LUA_TSTRING)
#define ttisfunction(o) checktype((o), LUA_TFUNCTION)
```

### 11.2 数组操作宏

```c
/* ltable.h */
#define getArrTag(t, k) (cast(lu_byte *, (t)->array) + sizeof(unsigned) + (k))
#define getArrVal(t, k) ((t)->array - 1 - (k))
#define lenhint(t) cast(unsigned *, (t)->array)

#define arr2obj(h, k, val) ((val)->tt_ = *getArrTag(h, (k)), (val)->value_ = *getArrVal(h, (k)))
#define obj2arr(h, k, val) (*getArrTag(h, (k)) = (val)->tt_, *getArrVal(h, (k)) = (val)->value_)
#define farr2val(h, k, tag, res) ((res)->tt_ = tag, (res)->value_ = *getArrVal(h, (k)))
#define fval2arr(h, k, tag, val) (*tag = (val)->tt_, *getArrVal(h, (k)) = (val)->value_)

#define arraylimit(h) ((h)->mode == TABLE_ARRAY ? (h)->loglen : (h)->asize)
```

### 11.3 栈操作宏

```c
/* lapi.h / lvm.h */
#define RA(i) (base + GETARG_A(i))
#define vRA(i) s2v(RA(i))
#define RB(i) (base + GETARG_B(i))
#define vRB(i) s2v(RB(i))

/* 栈索引 */
#define lua_absindex(L, idx) ((idx > 0 || ispseudo(idx)) ? idx : cast_int(L->top.p - L->ci->func.p) + idx)
#define lua_gettop(L) cast_int(L->top.p - (L->ci->func.p + 1))
```

### 11.4 核心函数

```c
/* ltable.c */
Table *luaH_new(lua_State *L);
Table *luaH_newarray(lua_State *L);
void luaH_resize(lua_State *L, Table *t, unsigned nasize, unsigned nhsize);
void luaH_resizearray(lua_State *L, Table *t, unsigned nasize);
lu_byte luaH_get(Table *t, const TValue *key, TValue *res);
lu_byte luaH_getint(Table *t, lua_Integer key, TValue *res);
void luaH_set(lua_State *L, Table *t, const TValue *key, TValue *value);
void luaH_setint(lua_State *L, Table *t, lua_Integer key, TValue *value);
lua_Unsigned luaH_getn(lua_State *L, Table *t);

/* lvm.c */
void luaV_execute(lua_State *L, CallInfo *ci);
void luaV_objlen(lua_State *L, StkId ra, const TValue *rb);
lu_byte luaV_finishget(lua_State *L, const TValue *t, TValue *key, StkId val, lu_byte tag);
void luaV_finishset(lua_State *L, const TValue *t, TValue *key, TValue *val, int hres);

/* ldo.c */
CallInfo *luaD_precall(lua_State *L, StkId func, int nresults);
void luaD_call(lua_State *L, StkId func, int nResults);
void luaD_callnoyield(lua_State *L, StkId func, int nResults);
void luaD_poscall(lua_State *L, CallInfo *ci, int nres);
```

### 11.5 Registry 引用机制变更

由于拆分了map和list,Lua 原生的注册表引用机制发生了变更。
原生的 `luaL_ref(L, LUA_REGISTRYINDEX)` 生成的整数句柄，在底层依然依赖于 Lua 的原生 Table 操作。

目前修改了 `lstate.c` 和 `lapi.c`，使得 `luaL_ref` 分配的整数句柄直接映射到 `global_State` 内部维护的一个自定义连续 C 数组 (`g->registry_array`) 中。这绕过了所有的 Table 查找开销，实现了真正的 O(1) 物理内存寻址。

**3. 对 C++ 侧代码的破坏性变更（Breaking Change）：**
因为数据不再存在普通的 Lua Table 中，**不能**再用传统的表操作接口去读取它。
- ❌ **禁止使用**：`lua_rawgeti(L, LUA_REGISTRYINDEX, ref)`
- ❌ **禁止使用**：`lua_rawseti(L, LUA_REGISTRYINDEX, ref)`

**4. 替代方案（新 API）：**
在 `lua.h` 中暴露了专属的极速访问接口，请在所有交互代码中使用它们：
```c
// 获取 registry 引用
LUA_API int lua_getref(lua_State *L, int ref);

// 更新 registry 引用
LUA_API void lua_setref(lua_State *L, int ref);
```

### 11.6 原生 class 机制
SPT 原生类的实例化： 靠的是类表上的 __call 元方法拦截，然后去调用内部隐式的 __init。

---

## 版本信息

- **Lua 版本**: 5.5 (2026 官方版本修改)
- **SPT 修改版本**: 1.0
- **文档最后更新**: 2026-03-12

---

## 参考资源

- [Lua 5.5 官方文档](https://www.lua.org/manual/5.5/)
- [SPT 项目仓库](https://github.com/Xarvie/spt)
- [sol2 文档](https://sol2.readthedocs.io/)
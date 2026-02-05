/*
 * SPT Script Language - Complete C API
 * =====================================
 * Stack-Based API
 *
 * Design Principles:
 * 1. Virtual Stack: All operations use stack indices (0 allocation in user code).
 * 2. Raw Power: Direct access to VM primitives.
 * 3. Symmetry: Script(Function/Instance) <-> C(CFunction/CInstance).
 * 4. Full Pipeline: Parse -> Compile -> Execute workflow support.
 *
 * Indexing:
 * - Positive (+i): Absolute index from stack base (1 = bottom).
 * - Negative (-i): Relative index from stack top (-1 = top).
 * - Pseudo-indices: SPT_REGISTRYINDEX for special tables, SPT_UPVALUEINDEX(i) for upvalues.
 */

/*
================================================================================
                  SPT 虚拟机架构与绑定层开发指南
================================================================================

一、核心概念
------------

  |                        spt_State                            |
  |  +---------+  +-------------+  +-------------------------+  |
  |  |   VM    |  | FiberObject |  |      StringPool         |  |
  |  | (执行器) |  |  (执行上下文) |  |     (字符串池)          |  |
  |  +---------+  +-------------+  +-------------------------+  |
  +-------------------------------------------------------------+

                                                                                              spt_State
- 绑定层状态，持有 VM 和当前 Fiber VM           - 字节码执行引擎，管理全局状态、GC、调用栈
      FiberObject  - 协程/执行上下文，拥有独立的值栈和调用帧栈
            CallFrame    - 单次函数调用的上下文（IP、局部变量槽、返回信息）


      二、值栈模型
      ------------

      SPT 使用基于栈的虚拟机，所有操作通过值栈进行：

      低地址                              高地址
  |                                    |
  v                                    v
      +---+---+---+---+---+---+---+---+---+---+---+
  | G | G | L | L | L | T | T |   |   |   |   |
  +---+---+---+---+---+---+---+---+---+---+---+
  |       |           |       |
  |       |           |       +-- stackTop (栈顶指针)
  |       |           +-- 临时值 (表达式求值)
  |       +-- 当前帧的局部变量 (slots)
            +-- 上一帧的数据

              索引规则：
              正索引 - 从栈底开始 (1 = 第一个元素)
                  负索引 - 从栈顶开始 (-1 = 栈顶元素)


                  三、函数调用协议
            ----------------

            调用前准备:
            1. push 函数
            2. push 参数1
            3. push 参数2
            ...
            4. push 参数N
            5. 调用 spt_call(S, N, nresults)

                栈的变化:
  调用前:  [...] [func] [arg1] [arg2] ... [argN]
        ^                         ^
          funcIdx                   stackTop

          调用后:  [...] [ret1] [ret2] ... [retM]
        ^                   ^
              baseHeight          stackTop

              关键规则:
  - 函数和参数在调用后被消耗（从栈上移除）
              - 返回值从原函数位置开始放置
              - nresults = SPT_MULTRET (-1) 时保留所有返回值
                           - nresults = N 时精确返回 N 个值（不足补 nil，多余丢弃）


  四、CallFrame 结构详解
  ----------------------

  struct CallFrame {
Closure *closure;       // 当前执行的函数
const uint32_t *ip;     // 指令指针
Value *slots;           // 局部变量起始位置 (参数从 slots[0] 开始)
Value *returnTo;        // 返回值写入位置 (nullptr 表示根帧)
int expectedResults;    // 期望的返回值数量 (-1 = MULTRET)
size_t deferBase;       // defer 栈基准
};

returnTo 的作用:

  调用者帧:  [...] [func] [arg1] [arg2]
             ^
             returnTo ------+
  |
  被调用帧:  [arg1] [arg2] [local1] [local2] ...
             ^
             slots

                 返回时:    [...] [ret1] [ret2]
                          ^
                          returnTo 位置开始写入


                              五、Native 函数开发规范
                          -----------------------

                          A) C API 函数签名 (绑定层使用):
  typedef int (*spt_CFunction)(spt_State *S);

示例 - 实现 add(a, b):

                      int capi_add(spt_State *S) {
// 1. 参数验证 (通过栈索引访问，1 = 第一个参数)
if (!spt_isnumber(S, 1) || !spt_isnumber(S, 2)) {
  spt_pushstring(S, "add expects 2 numbers");
  return -1; // 返回负数表示错误
}

// 2. 获取参数 (正索引从栈底开始)
double a = spt_tonumber(S, 1);
double b = spt_tonumber(S, 2);

// 3. push 返回值到栈顶
spt_pushnumber(S, a + b);

// 4. 返回返回值数量
return 1;
}

C API 函数的关键规则:
  参数访问   - 通过正栈索引 (1, 2, 3...) 访问
      返回值     - 必须 push 到栈顶
      返回数量   - 函数返回 int 表示 push 了多少个返回值
      错误处理   - push 错误信息后返回负数

                          B) 内部 VM 原生函数签名 (引擎内部使用):
  typedef int (*NativeFunction)(VM *vm, Closure *closure, int argCount, Value *args);

示例 - 内部 add(a, b):

                      int native_add(VM *vm, Closure *closure, int argCount, Value *args) {
// args[0] = self/receiver (如果是方法调用), args[1..] = 参数
// 对于普通函数: args[0] = 第一个参数
if (argCount < 2) {
  vm->runtimeError("add expects 2 arguments");
  return 0;
}

double a = args[0].asFloat();
double b = args[1].asFloat();

vm->currentFiber()->push(Value::number(a + b));
return 1;
}

内部 Native 函数的关键规则:
  参数访问   - 通过 args[i] 访问，不要 pop
      返回值     - 必须 push 到栈顶
      返回数量   - 函数返回 int 表示 push 了多少个返回值
      错误处理   - 调用 vm->runtimeError() 后返回 0
  不要修改   - args 指向的是栈内存，修改会破坏调用者数据


  六、绑定层 API 速查
  -------------------

  栈操作:
  spt_gettop(S)                  获取栈顶位置 (元素数量)
      spt_settop(S, newTop)          设置栈顶 (可用于 pop 多个值)

          spt_pushnil(S)                 push nil
  spt_pushinteger(S, 42)         push 整数
  spt_pushnumber(S, 3.14)        push 浮点数
  spt_pushstring(S, "hello")     push 字符串
  spt_pushboolean(S, true)       push 布尔值
  spt_pushcfunction(S, fn)       push C 函数 (无 upvalue 的便捷宏)

spt_tointeger(S, idx)          获取整数 (不移除)
  spt_tonumber(S, idx)           获取浮点数
  spt_tostring(S, idx)           获取字符串
spt_toboolean(S, idx)          获取布尔值

  spt_isnil(S, idx)              类型检查
spt_isnumber(S, idx)
      spt_isstring(S, idx)

          函数调用:
  spt_call(S, nargs, nresults)           调用栈上的函数
  spt_pcall(S, nargs, nresults, errfunc) 保护模式调用 (捕获错误)

      全局变量:
  spt_getglobal(S, "name")       获取全局变量，push 到栈顶
  spt_setglobal(S, "name")       将栈顶值设为全局变量

  表/容器操作:
  spt_newmap(S, cap)             创建新 Map (push 到栈顶)
  spt_newlist(S, cap)            创建新 List (push 到栈顶)
  spt_getfield(S, idx, "key")    获取 Map/Instance 字段 -> push
  spt_setfield(S, idx, "key")    设置 Map/Instance 字段，pop 栈顶值
  spt_rawget(S, idx)             原始访问 (不触发魔术方法)
      spt_rawset(S, idx)


          七、典型使用模式
  ----------------

  模式1 - 调用脚本函数:

  spt_getglobal(S, "myFunc");     // push 函数
spt_pushinteger(S, 10);         // push 参数1
spt_pushstring(S, "hello");     // push 参数2
if (spt_pcall(S, 2, 1, 0) != SPT_OK) {
const char *err = spt_tostring(S, -1);
printf("Error: %s\n", err);
spt_pop(S, 1);
} else {
int result = spt_tointeger(S, -1);
spt_pop(S, 1);
}

模式2 - 注册 Native 函数库:

  static int capi_sin(spt_State *S) {
double x = spt_tonumber(S, 1);
spt_pushnumber(S, sin(x));
return 1;
}

void register_math_lib(spt_State *S) {
spt_newmap(S, 4);
spt_pushcfunction(S, capi_sin);
spt_setfield(S, -2, "sin");
spt_setglobal(S, "math");
}

模式3 - 迭代 Map:

  spt_pushnil(S);  // 初始 prevKey (nil 表示从头开始)
while (spt_mapnext(S, mapIdx)) {
// 栈: [..., nextKey, value]
const char *key = spt_tostring(S, -2);
// 处理 value...
spt_pop(S, 1);  // 移除 value，保留 key 作为下次迭代的 prevKey
}


八、常见陷阱
------------

             栈不平衡       函数结束时栈元素数量与预期不符
                 -> 使用 spt_gettop 在入口和出口检查

                     索引失效       在 push/pop 后使用旧的绝对索引
      -> 优先使用负索引，或在操作后重新计算

          GC 问题        持有 C 指针指向的对象被 GC 回收
      -> 将需要保留的对象 push 到栈上或使用 registry

          Native 返回值  忘记 push 返回值或返回错误的数量
      -> 始终确保 push 数量与返回值匹配

          字符串生命周期 spt_tostring 返回的指针可能失效
      -> 立即复制字符串或确保不触发 GC


          九、调试技巧
  ------------

  void dump_stack(spt_State *S) {
int top = spt_gettop(S);
printf("Stack [%d]: ", top);
for (int i = 1; i <= top; i++) {
  int t = spt_type(S, i);
  switch (t) {
  case SPT_TNIL:     printf("nil "); break;
  case SPT_TINT:     printf("%lld ", (long long)spt_tointeger(S, i)); break;
  case SPT_TFLOAT:   printf("%g ", spt_tonumber(S, i)); break;
  case SPT_TSTRING:  printf("'%s' ", spt_tostring(S, i)); break;
  case SPT_TBOOL:    printf("%s ", spt_toboolean(S, i) ? "true" : "false"); break;
  default:           printf("[%s] ", spt_typename(S, t)); break;
  }
}
printf("\n");
}

================================================================================
*/
#ifndef SPT_H
#define SPT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. VERSION & CONFIGURATION
 * ============================================================================ */

#define SPT_VERSION_MAJOR 1
#define SPT_VERSION_MINOR 0
#define SPT_VERSION_PATCH 0
#define SPT_VERSION_NUM (SPT_VERSION_MAJOR * 10000 + SPT_VERSION_MINOR * 100 + SPT_VERSION_PATCH)
#define SPT_VERSION_STRING "1.0.0"

#if defined(_WIN32) || defined(_WIN64) /* { */
#if defined(SPT_BUILD_AS_DLL)          /* { */
#if defined(SPT_EXPORTS)               /* { */
#define SPT_API __declspec(dllexport)
#else /* }{ */
#define SPT_API __declspec(dllimport)
#endif /* } */
#define SPT_API_CLASS SPT_API
#else /* }{ */
#define SPT_API extern
#define SPT_API_CLASS
#endif                        /* } */
#else                         /* }{ */
// 非 Windows 平台（Linux/macOS）
#if defined(SPT_BUILD_AS_DLL) /* { */
#define SPT_API __attribute__((visibility("default")))
#define SPT_API_CLASS __attribute__((visibility("default")))
#else /* }{ */
#define SPT_API extern
#define SPT_API_CLASS
#endif /* } */
#endif /* } */

/* ============================================================================
 * 2. BASIC TYPES
 * ============================================================================ */

/* Opaque state handle - wraps the internal VM and current Fiber */
typedef struct spt_State spt_State;

/* Opaque AST handle - represents parsed source code */
typedef struct spt_Ast spt_Ast;

/* Opaque compiled chunk handle - represents bytecode */
typedef struct spt_Chunk spt_Chunk;

/* Opaque compiler handle */
typedef struct spt_Compiler spt_Compiler;

/* Numeric types */
typedef int64_t spt_Int;
typedef double spt_Float;

/* Stack index type */
typedef int spt_Index;

/* C function signature
 * Parameters:
 *   S    - The state
 * Returns:
 *   Number of return values pushed onto the stack
 */
typedef int (*spt_CFunction)(spt_State *S);

/* Context type (capable of holding a pointer or an integer) */
typedef intptr_t spt_KContext;

/* Continuation function type.
 * Parameters:
 * S      - The state
 * status - Status code upon resumption (usually SPT_OK or SPT_YIELD)
 * ctx    - Saved context data
 */
typedef int (*spt_KFunction)(spt_State *S, int status, spt_KContext ctx);

/* Error handler callback
 * Parameters:
 *   S       - The state
 *   message - Error message
 *   line    - Line number (-1 if unknown)
 *   ud      - User data passed to spt_seterrorhandler
 */
typedef void (*spt_ErrorHandler)(spt_State *S, const char *message, int line, void *ud);

/* Print handler callback
 * Parameters:
 *   S       - The state
 *   message - Message to print
 *   ud      - User data passed to spt_setprinthandler
 */
typedef void (*spt_PrintHandler)(spt_State *S, const char *message, void *ud);

/* Compile error callback (for Compiler)
 * Parameters:
 *   message - Error message
 *   line    - Line number
 *   column  - Column number
 *   source  - Source file name
 *   ud      - User data
 */
typedef void (*spt_CompileErrorHandler)(const char *message, int line, int column,
                                        const char *source, void *ud);

/* Function registration entry */
typedef struct {
  const char *name;
  spt_CFunction func;
  int arity; /* Number of parameters, -1 for variadic */
} spt_Reg;

/* Method registration entry (for classes) */
typedef struct {
  const char *name;
  spt_CFunction func;
  int arity;
  bool isStatic; /* true for static methods */
} spt_MethodReg;

/* ============================================================================
 * 3. CONSTANTS
 * ============================================================================ */

/* Status codes */
enum {
  SPT_OK = 0,         /* Success */
  SPT_YIELD = 1,      /* Fiber yielded */
  SPT_ERRRUN = 2,     /* Runtime error */
  SPT_ERRSYNTAX = 3,  /* Syntax/parse error */
  SPT_ERRCOMPILE = 4, /* Compilation error */
  SPT_ERRMEM = 5,     /* Memory allocation error */
  SPT_ERRERR = 6,     /* Error in error handler */
  SPT_ERRFILE = 7     /* File I/O error */
};

/* Type enumeration (matches internal ValueType) */
enum {
  SPT_TNONE = -1,
  SPT_TNIL = 0,
  SPT_TBOOL = 1,
  SPT_TINT = 2,
  SPT_TFLOAT = 3,
  SPT_TSTRING = 4,
  SPT_TLIST = 5,
  SPT_TMAP = 6,
  SPT_TOBJECT = 7,  /* Script Instance */
  SPT_TCLOSURE = 8, /* Function (script or native) */
  SPT_TCLASS = 9,
  SPT_TUPVALUE = 10,
  SPT_TFIBER = 11,
  SPT_TCINSTANCE = 12,    /* C Instance (NativeObject) */
  SPT_TLIGHTUSERDATA = 13 /* Light userdata (raw pointer, not GC managed) */
};

/* Fiber states */
enum {
  SPT_FIBER_NEW = 0,
  SPT_FIBER_RUNNING = 1,
  SPT_FIBER_SUSPENDED = 2,
  SPT_FIBER_DONE = 3,
  SPT_FIBER_ERROR = 4
};

/* Magic method indices (matches internal MagicMethod enum) */
enum {
  /* Lifecycle */
  SPT_MM_INIT = 0, /* __init (constructor) */
  SPT_MM_GC = 1,   /* __gc (finalizer/destructor) */

  /* Property access */
  SPT_MM_GET = 2,       /* __get (property get fallback) */
  SPT_MM_SET = 3,       /* __set (property set intercept) */
  SPT_MM_INDEX_GET = 4, /* __getitem (subscript read obj[key]) */
  SPT_MM_INDEX_SET = 5, /* __setitem (subscript write obj[key] = value) */

  /* Arithmetic operators */
  SPT_MM_ADD = 6,   /* __add (+) */
  SPT_MM_SUB = 7,   /* __sub (-) */
  SPT_MM_MUL = 8,   /* __mul (*) */
  SPT_MM_DIV = 9,   /* __div (/) */
  SPT_MM_MOD = 10,  /* __mod (%) */
  SPT_MM_POW = 11,  /* __pow (**) */
  SPT_MM_UNM = 12,  /* __unm (unary minus -x) */
  SPT_MM_IDIV = 13, /* __idiv (integer division ~/) */

  /* Comparison operators */
  SPT_MM_EQ = 14, /* __eq (==) */
  SPT_MM_LT = 15, /* __lt (<) */
  SPT_MM_LE = 16, /* __le (<=) */

  /* Bitwise operators */
  SPT_MM_BAND = 17, /* __band (&) */
  SPT_MM_BOR = 18,  /* __bor (|) */
  SPT_MM_BXOR = 19, /* __bxor (^) */
  SPT_MM_BNOT = 20, /* __bnot (~) */
  SPT_MM_SHL = 21,  /* __shl (<<) */
  SPT_MM_SHR = 22,  /* __shr (>>) */

  SPT_MM_MAX = 23 /* Total count (must be last) */
};

/* Class flags for quick magic method detection (bitmask) */
enum {
  SPT_CLASS_NONE = 0,

  /* Lifecycle */
  SPT_CLASS_HAS_INIT = (1u << SPT_MM_INIT),
  SPT_CLASS_HAS_GC = (1u << SPT_MM_GC),

  /* Property access */
  SPT_CLASS_HAS_GET = (1u << SPT_MM_GET),
  SPT_CLASS_HAS_SET = (1u << SPT_MM_SET),
  SPT_CLASS_HAS_INDEX_GET = (1u << SPT_MM_INDEX_GET),
  SPT_CLASS_HAS_INDEX_SET = (1u << SPT_MM_INDEX_SET),

  /* Arithmetic operators */
  SPT_CLASS_HAS_ADD = (1u << SPT_MM_ADD),
  SPT_CLASS_HAS_SUB = (1u << SPT_MM_SUB),
  SPT_CLASS_HAS_MUL = (1u << SPT_MM_MUL),
  SPT_CLASS_HAS_DIV = (1u << SPT_MM_DIV),
  SPT_CLASS_HAS_MOD = (1u << SPT_MM_MOD),
  SPT_CLASS_HAS_POW = (1u << SPT_MM_POW),
  SPT_CLASS_HAS_UNM = (1u << SPT_MM_UNM),
  SPT_CLASS_HAS_IDIV = (1u << SPT_MM_IDIV),

  /* Comparison operators */
  SPT_CLASS_HAS_EQ = (1u << SPT_MM_EQ),
  SPT_CLASS_HAS_LT = (1u << SPT_MM_LT),
  SPT_CLASS_HAS_LE = (1u << SPT_MM_LE),

  /* Bitwise operators */
  SPT_CLASS_HAS_BAND = (1u << SPT_MM_BAND),
  SPT_CLASS_HAS_BOR = (1u << SPT_MM_BOR),
  SPT_CLASS_HAS_BXOR = (1u << SPT_MM_BXOR),
  SPT_CLASS_HAS_BNOT = (1u << SPT_MM_BNOT),
  SPT_CLASS_HAS_SHL = (1u << SPT_MM_SHL),
  SPT_CLASS_HAS_SHR = (1u << SPT_MM_SHR),

  /* Compound flags for quick category detection */
  SPT_CLASS_HAS_ANY_ARITHMETIC = SPT_CLASS_HAS_ADD | SPT_CLASS_HAS_SUB | SPT_CLASS_HAS_MUL |
                                 SPT_CLASS_HAS_DIV | SPT_CLASS_HAS_MOD | SPT_CLASS_HAS_POW |
                                 SPT_CLASS_HAS_UNM | SPT_CLASS_HAS_IDIV,

  SPT_CLASS_HAS_ANY_COMPARISON = SPT_CLASS_HAS_EQ | SPT_CLASS_HAS_LT | SPT_CLASS_HAS_LE,

  SPT_CLASS_HAS_ANY_BITWISE = SPT_CLASS_HAS_BAND | SPT_CLASS_HAS_BOR | SPT_CLASS_HAS_BXOR |
                              SPT_CLASS_HAS_BNOT | SPT_CLASS_HAS_SHL | SPT_CLASS_HAS_SHR,

  SPT_CLASS_HAS_ANY_INDEX = SPT_CLASS_HAS_INDEX_GET | SPT_CLASS_HAS_INDEX_SET,

  SPT_CLASS_HAS_ANY_PROPERTY = SPT_CLASS_HAS_GET | SPT_CLASS_HAS_SET
};

/* Pseudo-indices for special locations */
#define SPT_REGISTRYINDEX (-1000000)

/* Upvalue pseudo-index - accesses upvalues in C closures
 * SPT_UPVALUEINDEX(1) = -1000001, SPT_UPVALUEINDEX(2) = -1000002, etc.
 */
#define SPT_UPVALUEINDEX(i) (SPT_REGISTRYINDEX - (i))

/* Multiple return values marker */
#define SPT_MULTRET (-1)

/* Reference system constants */
#define SPT_REFNIL (-1)
#define SPT_NOREF (-2)

/* ============================================================================
 * 4. STATE MANAGEMENT
 * ============================================================================ */

/*
 * Create a new SPT state with default configuration.
 * Returns NULL on failure (e.g., memory allocation).
 */
SPT_API spt_State *spt_newstate(void);

/*
 * Create a new SPT state with custom configuration.
 * Parameters:
 *   stackSize    - Initial stack size (0 for default: 256KB)
 *   heapSize     - Maximum heap size (0 for default: 64MB)
 *   enableGC     - Enable garbage collection
 */
SPT_API spt_State *spt_newstateex(size_t stackSize, size_t heapSize, bool enableGC);

/*
 * Close and free all resources associated with the state.
 * After this call, the state pointer is invalid.
 */
SPT_API void spt_close(spt_State *S);

/*
 * Get the currently executing fiber's state.
 * Returns S if called from the main fiber.
 */
SPT_API spt_State *spt_getcurrent(spt_State *S);

/*
 * Get the main state (main fiber) from any state.
 */
SPT_API spt_State *spt_getmain(spt_State *S);

/*
 * Set user data pointer associated with the state.
 */
SPT_API void spt_setuserdata(spt_State *S, void *ud);

/*
 * Get user data pointer previously set with spt_setuserdata.
 */
SPT_API void *spt_getuserdata(spt_State *S);

/* ============================================================================
 * 5. STACK OPERATIONS
 * ============================================================================ */

/*
 * Get the current stack top index (number of elements).
 */
SPT_API int spt_gettop(spt_State *S);

/*
 * Set the stack top to a specific index.
 * If idx > current top, pushes nils.
 * If idx < current top, pops elements.
 * If idx is negative, sets relative to current top.
 */
SPT_API void spt_settop(spt_State *S, int idx);

/*
 * Push a copy of the value at the given index.
 */
SPT_API void spt_pushvalue(spt_State *S, int idx);

/*
 * Rotate the stack elements between idx and top.
 * n > 0: rotate towards top
 * n < 0: rotate towards bottom
 */
SPT_API void spt_rotate(spt_State *S, int idx, int n);

/*
 * Copy value from one stack position to another without removing.
 */
SPT_API void spt_copy(spt_State *S, int fromidx, int toidx);

/*
 * Insert top element at the given index, shifting elements up.
 */
SPT_API void spt_insert(spt_State *S, int idx);

/*
 * Remove element at the given index, shifting elements down.
 */
SPT_API void spt_remove(spt_State *S, int idx);

/*
 * Replace element at the given index with top element, then pop top.
 */
SPT_API void spt_replace(spt_State *S, int idx);

/*
 * Ensure at least n extra stack slots are available.
 * Returns 1 on success, 0 on failure.
 */
SPT_API int spt_checkstack(spt_State *S, int n);

/*
 * Move n values from state 'from' to state 'to'.
 * Values are popped from 'from' and pushed onto 'to'.
 *
 * NOTE: If 'from' has fewer than n elements, the function will
 * only move the available elements (clamped to stack depth).
 */
SPT_API void spt_xmove(spt_State *from, spt_State *to, int n);

/*
 * Convert a possibly negative index to an absolute positive index.
 * Returns 0 if index is invalid.
 */
SPT_API int spt_absindex(spt_State *S, int idx);

/* =============================================================================
 * IMPORTANT: Stack Pointer Safety
 * =============================================================================
 * Functions that return raw pointers to stack slots (internal use only) can
 * return DANGLING POINTERS if the stack is reallocated. The stack may grow
 * during:
 *   - spt_push*() operations
 *   - spt_call() / spt_pcall()
 *   - Any function that may push values
 *
 * SAFE PATTERNS for C extensions:
 *   1. Use index-based APIs: spt_copy(), spt_replace(), spt_rotate()
 *   2. Use getter/setter functions: spt_toint(), spt_tostring(), etc.
 *   3. Complete all read operations before any push operations
 *   4. Re-compute indices after any operation that may grow the stack
 * ===========================================================================*/

/* Convenience macros */
#define spt_pop(S, n) spt_settop(S, -(n) - 1)
#define spt_isnone(S, n) (spt_type(S, (n)) == SPT_TNONE)
#define spt_isnoneornil(S, n) (spt_type(S, (n)) <= 0)
#define spt_isnil(S, n) (spt_type(S, (n)) == SPT_TNIL)

/* ============================================================================
 * 6. PUSH VALUES
 * ============================================================================ */

SPT_API void spt_pushnil(spt_State *S);
SPT_API void spt_pushbool(spt_State *S, int b);
SPT_API void spt_pushint(spt_State *S, spt_Int n);
SPT_API void spt_pushfloat(spt_State *S, spt_Float n);

/*
 * Push a null-terminated string (copies the string).
 */
SPT_API void spt_pushstring(spt_State *S, const char *s);

/*
 * Push a string with explicit length (can contain nulls).
 */
SPT_API void spt_pushlstring(spt_State *S, const char *s, size_t len);

/*
 * Push a formatted string (like printf).
 * Returns the pushed string.
 */
SPT_API const char *spt_pushfstring(spt_State *S, const char *fmt, ...);

/*
 * Push a formatted string with va_list.
 */
SPT_API const char *spt_pushvfstring(spt_State *S, const char *fmt, va_list ap);

/*
 * Push a light userdata (raw pointer, not GC managed).
 */
SPT_API void spt_pushlightuserdata(spt_State *S, void *p);

/* ============================================================================
 * 7. TYPE CHECKING & CONVERSION
 * ============================================================================ */

/*
 * Get the type of the value at the given index.
 * Returns SPT_TNONE if index is invalid.
 */
SPT_API int spt_type(spt_State *S, int idx);

/*
 * Get the type name as a string.
 */
SPT_API const char *spt_typename(spt_State *S, int tp);

/* Quick type checks */
SPT_API int spt_isbool(spt_State *S, int idx);
SPT_API int spt_isint(spt_State *S, int idx);
SPT_API int spt_isfloat(spt_State *S, int idx);
SPT_API int spt_isnumber(spt_State *S, int idx); /* int or float */
SPT_API int spt_isstring(spt_State *S, int idx);
SPT_API int spt_islist(spt_State *S, int idx);
SPT_API int spt_ismap(spt_State *S, int idx);
SPT_API int spt_isfunction(spt_State *S, int idx);  /* closure (script or native) */
SPT_API int spt_iscfunction(spt_State *S, int idx); /* native closure only */
SPT_API int spt_isclass(spt_State *S, int idx);
SPT_API int spt_isobject(spt_State *S, int idx);    /* script instance */
SPT_API int spt_iscinstance(spt_State *S, int idx); /* native instance */
SPT_API int spt_isfiber(spt_State *S, int idx);
SPT_API int spt_islightuserdata(spt_State *S, int idx); /* light userdata */

/*
 * Check if value is "truthy" (non-nil, non-false, non-zero).
 */
SPT_API int spt_toboolean(spt_State *S, int idx);

/* Value conversion (zero-allocation for existing values) */
SPT_API int spt_tobool(spt_State *S, int idx);
SPT_API spt_Int spt_toint(spt_State *S, int idx);
SPT_API spt_Int spt_tointx(spt_State *S, int idx, int *isnum);
SPT_API spt_Float spt_tofloat(spt_State *S, int idx);
SPT_API spt_Float spt_tofloatx(spt_State *S, int idx, int *isnum);

/*
 * Get string value. Returns NULL if not a string.
 * The returned pointer is valid until the value is GC'd.
 * If len is not NULL, stores the string length.
 */
SPT_API const char *spt_tostring(spt_State *S, int idx, size_t *len);

/*
 * Get the data pointer from a CInstance.
 * Returns NULL if not a CInstance.
 */
SPT_API void *spt_tocinstance(spt_State *S, int idx);

/*
 * Get fiber state from stack. Returns NULL if not a fiber.
 */
SPT_API spt_State *spt_tofiber(spt_State *S, int idx);

/*
 * Get a raw pointer representation of any value (for identity comparison).
 */
SPT_API const void *spt_topointer(spt_State *S, int idx);

/*
 * Get light userdata pointer. Returns NULL if not light userdata.
 */
SPT_API void *spt_tolightuserdata(spt_State *S, int idx);

/*
 * Compare two values.
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 * For non-comparable types, returns based on type ordering.
 */
SPT_API int spt_compare(spt_State *S, int idx1, int idx2);

/*
 * Check equality of two values.
 */
SPT_API int spt_equal(spt_State *S, int idx1, int idx2);

/*
 * Check raw equality (pointer comparison for objects).
 */
SPT_API int spt_rawequal(spt_State *S, int idx1, int idx2);

/*
 * Get value from table/object without invoking magic methods (__get/__index).
 * Supported types: Map, List, Instance, NativeInstance.
 * Stack: [..., key] -> spt_rawget(S, idx) -> [..., result]
 * Pops key, pushes result. Returns the type of the pushed value.
 */
SPT_API int spt_rawget(spt_State *S, int idx);

/*
 * Set value in table/object without invoking magic methods (__set/__newindex).
 * Supported types: Map, List, Instance, NativeInstance.
 * Stack: [..., key, value] -> spt_rawset(S, idx) -> [...]
 * Pops both key and value.
 */
SPT_API void spt_rawset(spt_State *S, int idx);

/* ============================================================================
 * 8. LIST OPERATIONS
 * ============================================================================ */

/*
 * Create a new list and push it.
 * capacity is a hint for pre-allocation.
 */
SPT_API void spt_newlist(spt_State *S, int capacity);

/*
 * Get the length of a list at idx.
 */
SPT_API int spt_listlen(spt_State *S, int idx);

/*
 * Append top value to list at idx, pop the value.
 */
SPT_API void spt_listappend(spt_State *S, int idx);

/*
 * Push list[n] onto the stack.
 */
SPT_API void spt_listgeti(spt_State *S, int idx, int n);

/*
 * Set list[n] = top value, pop the value.
 */
SPT_API void spt_listseti(spt_State *S, int idx, int n);

/*
 * Insert value at position n (shifts elements).
 * Stack: [..., value] -> spt_listinsert(S, listidx, n) -> [...]
 */
SPT_API void spt_listinsert(spt_State *S, int idx, int n);

/*
 * Remove and push element at position n.
 */
SPT_API void spt_listremove(spt_State *S, int idx, int n);

/*
 * Clear all elements from list.
 */
SPT_API void spt_listclear(spt_State *S, int idx);

/* ============================================================================
 * 9. MAP OPERATIONS
 * ============================================================================ */

/*
 * Create a new map and push it.
 */
SPT_API void spt_newmap(spt_State *S, int capacity);

/*
 * Get number of entries in map.
 */
SPT_API int spt_maplen(spt_State *S, int idx);

/*
 * Push map[key] where key is at stack top, pop key.
 * Returns the type of the pushed value.
 */
SPT_API int spt_getmap(spt_State *S, int idx);

/*
 * Set map[key] = value.
 * Stack: [..., key, value] -> spt_setmap(S, mapidx) -> [...]
 */
SPT_API void spt_setmap(spt_State *S, int idx);

/*
 * Push map[key] where key is a string.
 * Returns the type of the pushed value.
 */
SPT_API int spt_getfield(spt_State *S, int idx, const char *key);

/*
 * Set map[key] = top value.
 * Stack: [..., value] -> spt_setfield(S, mapidx, "key") -> [...]
 */
SPT_API void spt_setfield(spt_State *S, int idx, const char *key);

/*
 * Check if map contains key (at stack top), pop key.
 */
SPT_API int spt_haskey(spt_State *S, int idx);

/*
 * Remove key from map, push the removed value.
 * Stack: [..., key] -> spt_mapremove(S, mapidx) -> [..., value]
 */
SPT_API void spt_mapremove(spt_State *S, int idx);

/*
 * Clear all entries from map.
 */
SPT_API void spt_mapclear(spt_State *S, int idx);

/*
 * Push a list of all keys in the map.
 */
SPT_API void spt_mapkeys(spt_State *S, int idx);

/*
 * Push a list of all values in the map.
 */
SPT_API void spt_mapvalues(spt_State *S, int idx);

/*
 * Iteration: Get next key-value pair.
 * Stack: [..., prevKey] -> spt_mapnext(S, mapidx) -> [..., nextKey, value]
 * Push nil as prevKey to start iteration.
 * Returns 1 if found next pair, 0 if iteration complete.
 */
SPT_API int spt_mapnext(spt_State *S, int idx);

/* ============================================================================
 * 10. CLASS & OBJECT OPERATIONS
 * ============================================================================ */

/*
 * Create a new class and push it.
 */
SPT_API void spt_newclass(spt_State *S, const char *name);

/*
 * Bind a method to a class.
 * Stack: [..., function] -> spt_bindmethod(S, classidx, "name") -> [...]
 */
SPT_API void spt_bindmethod(spt_State *S, int class_idx, const char *name);

/*
 * Bind a static method/value to a class.
 * Stack: [..., value] -> spt_bindstatic(S, classidx, "name") -> [...]
 */
SPT_API void spt_bindstatic(spt_State *S, int class_idx, const char *name);

/*
 * Create a new instance by calling class constructor.
 * Stack: [..., class, arg1, ..., argN] -> spt_newinstance(S, nargs) -> [..., instance]
 */
SPT_API void spt_newinstance(spt_State *S, int nargs);

/*
 * Get property (field or method) from object/class.
 * Returns the type of the pushed value.
 */
SPT_API int spt_getprop(spt_State *S, int obj_idx, const char *name);

/*
 * Set property on object.
 * Stack: [..., value] -> spt_setprop(S, objidx, "name") -> [...]
 */
SPT_API void spt_setprop(spt_State *S, int obj_idx, const char *name);

/*
 * Check if object has a specific property.
 */
SPT_API int spt_hasprop(spt_State *S, int obj_idx, const char *name);

/*
 * Get the class of an instance.
 * Pushes the class object, returns its type.
 */
SPT_API int spt_getclass(spt_State *S, int obj_idx);

/*
 * Get class name as string.
 */
SPT_API const char *spt_classname(spt_State *S, int class_idx);

/*
 * Check if object is an instance of a class (or its subclass).
 * classidx should point to a class object.
 */
SPT_API int spt_isinstance(spt_State *S, int obj_idx, int class_idx);

/* ============================================================================
 * 11. C INSTANCE (NATIVE OBJECT)
 * ============================================================================ */

/*
 * Allocate a C instance with the given data size.
 * Returns pointer to the data area (like malloc).
 * The instance is pushed onto the stack.
 */
SPT_API void *spt_newcinstance(spt_State *S, size_t size);

/*
 * Allocate a C instance and associate it with a class.
 * Returns pointer to the data area.
 * Stack: [..., class] -> spt_newcinstanceof(S, size) -> [..., cinstance]
 */
SPT_API void *spt_newcinstanceof(spt_State *S, size_t size);

/*
 * Set the class of a C instance (for __gc finalizer support).
 * Stack: [..., class] -> spt_setcclass(S, cinstidx) -> [...]
 */
SPT_API void spt_setcclass(spt_State *S, int cinst_idx);

/*
 * Alias for backward compatibility.
 */
#define spt_setclass spt_setcclass

/*
 * Get the data pointer from a C instance.
 * Returns NULL if invalid.
 */
SPT_API void *spt_getcinstancedata(spt_State *S, int idx);

/*
 * Check if C instance is still valid (not finalized).
 */
SPT_API int spt_iscinstancevalid(spt_State *S, int idx);

/* ============================================================================
 * 12. MAGIC METHODS
 * ============================================================================ */

/*
 * Get the name of a magic method by index.
 * Returns NULL if mm is out of range.
 * Example: spt_magicmethodname(SPT_MM_ADD) returns "__add"
 */
SPT_API const char *spt_magicmethodname(int mm);

/*
 * Get magic method index from name.
 * Returns SPT_MM_MAX if name is not a magic method.
 * Example: spt_magicmethodindex("__add") returns SPT_MM_ADD
 */
SPT_API int spt_magicmethodindex(const char *name);

/*
 * Get the class flags (bitmask indicating which magic methods are defined).
 * Returns 0 if class_idx is not a valid class.
 */
SPT_API unsigned int spt_getclassflags(spt_State *S, int class_idx);

/*
 * Check if a class has a specific magic method.
 * mm: magic method index (SPT_MM_*)
 * Returns 1 if the class has the method, 0 otherwise.
 */
SPT_API int spt_hasmagicmethod(spt_State *S, int class_idx, int mm);

/*
 * Get a magic method from a class.
 * mm: magic method index (SPT_MM_*)
 * Pushes the method (closure) or nil if not defined.
 * Returns the type of the pushed value.
 */
SPT_API int spt_getmagicmethod(spt_State *S, int class_idx, int mm);

/*
 * Set a magic method on a class.
 * mm: magic method index (SPT_MM_*)
 * Stack: [..., closure] -> spt_setmagicmethod(S, classidx, mm) -> [...]
 * Note: Setting nil removes the magic method.
 */
SPT_API void spt_setmagicmethod(spt_State *S, int class_idx, int mm);

/*
 * Set a magic method on a class by name.
 * name: magic method name (e.g., "__add", "__gc")
 * Stack: [..., closure] -> spt_setmagicmethodbyname(S, classidx, "__add") -> [...]
 * Note: This is equivalent to spt_bindmethod but optimized for magic methods.
 *       It automatically updates the class's magic method VTable and flags.
 */
SPT_API void spt_setmagicmethodbyname(spt_State *S, int class_idx, const char *name);

/*
 * Check if an instance/object has a specific magic method (through its class).
 * Works with both script instances (SPT_TOBJECT) and C instances (SPT_TCINSTANCE).
 * mm: magic method index (SPT_MM_*)
 */
SPT_API int spt_objhasmagicmethod(spt_State *S, int obj_idx, int mm);

/*
 * Get a magic method from an instance/object (through its class).
 * Works with both script instances and C instances.
 * mm: magic method index (SPT_MM_*)
 * Pushes the method or nil.
 */
SPT_API int spt_objgetmagicmethod(spt_State *S, int obj_idx, int mm);

/*
 * Invoke a magic method on an object with arguments.
 * Stack: [..., obj, arg1, ..., argN] -> spt_callmagicmethod(S, mm, nargs, nresults)
 *     -> [..., res1, ..., resN]
 * mm: magic method index (SPT_MM_*)
 * nargs: number of arguments (excluding self/obj)
 * nresults: expected number of results (or SPT_MULTRET)
 * Returns SPT_OK on success, error code on failure.
 * If the object doesn't have the magic method, returns SPT_ERRRUN.
 */
SPT_API int spt_callmagicmethod(spt_State *S, int mm, int nargs, int nresults);

/*
 * Protected call of a magic method (catches errors).
 * Stack: [..., obj, arg1, ..., argN] -> spt_pcallmagicmethod(...)
 *     -> [..., res1, ...] or [..., errmsg]
 */
SPT_API int spt_pcallmagicmethod(spt_State *S, int mm, int nargs, int nresults, int errfunc);

/* ============================================================================
 * 13. C FUNCTION & CLOSURE
 * ============================================================================ */

/*
 * Push a C closure with nup upvalues.
 * Stack: [..., upval1, ..., upvalN] -> spt_pushcclosure(S, fn, nup) -> [..., closure]
 */
SPT_API void spt_pushcclosure(spt_State *S, spt_CFunction fn, int nup);

/*
 * Convenience: push a simple C function (no upvalues).
 */
#define spt_pushcfunction(S, f) spt_pushcclosure(S, f, 0)

/*
 * Get the n-th upvalue of a closure.
 * func_idx: index of the closure
 * n: upvalue index (1-based)
 * Pushes the upvalue onto the stack.
 */
SPT_API void spt_getupvalue(spt_State *S, int func_idx, int n);

/*
 * Set the n-th upvalue of a closure.
 * Stack: [..., value] -> spt_setupvalue(S, funcidx, n) -> [...]
 */
SPT_API void spt_setupvalue(spt_State *S, int func_idx, int n);

/*
 * Get number of upvalues in a closure.
 */
SPT_API int spt_getupvaluecount(spt_State *S, int func_idx);

/*
 * Get the arity (parameter count) of a function.
 * Returns -1 for variadic functions.
 */
SPT_API int spt_getarity(spt_State *S, int func_idx);

/* ============================================================================
 * 14. PARSING & COMPILATION (Full Pipeline Support)
 * ============================================================================ */

/*
 * Parse source code into an AST.
 * Parameters:
 *   source   - Source code string (can be NULL if filename is provided)
 *   filename - Source file name (used for error messages, and to read if source is NULL)
 * Returns:
 *   AST handle, or NULL on parse error.
 *   Use spt_getlasterror() for error details.
 */
SPT_API spt_Ast *spt_parse(const char *source, const char *filename);

/*
 * Parse source code from a file.
 */
SPT_API spt_Ast *spt_parsefile(const char *filename);

/*
 * Free an AST.
 */
SPT_API void spt_freeast(spt_Ast *ast);

/*
 * Create a new compiler instance.
 * moduleName - Name of the module being compiled
 * source     - Source file name (for error messages)
 */
SPT_API spt_Compiler *spt_newcompiler(const char *moduleName, const char *source);

/*
 * Free a compiler instance.
 */
SPT_API void spt_freecompiler(spt_Compiler *compiler);

/*
 * Set compile error handler.
 */
SPT_API void spt_setcompileerrorhandler(spt_Compiler *compiler, spt_CompileErrorHandler handler,
                                        void *ud);

/*
 * Compile an AST into bytecode.
 * Returns:
 *   Chunk handle, or NULL on compilation error.
 */
SPT_API spt_Chunk *spt_compile(spt_Compiler *compiler, spt_Ast *ast);

/*
 * Check if compiler encountered errors.
 */
SPT_API int spt_compilerhaserror(spt_Compiler *compiler);

/*
 * Get number of compile errors.
 */
SPT_API int spt_compilererrorcount(spt_Compiler *compiler);

/*
 * Get compile error at index.
 * Returns 1 if error exists, 0 otherwise.
 * Outputs are optional (can be NULL).
 */
SPT_API int spt_compilergeterror(spt_Compiler *compiler, int index, const char **message, int *line,
                                 int *column);

/*
 * Free a compiled chunk.
 */
SPT_API void spt_freechunk(spt_Chunk *chunk);

/*
 * Convenience: Parse and compile in one step.
 * Returns chunk or NULL on error.
 */
SPT_API spt_Chunk *spt_loadstring(spt_State *S, const char *source, const char *name);

/*
 * Convenience: Load and compile a file.
 */
SPT_API spt_Chunk *spt_loadfile(spt_State *S, const char *filename);

/*
 * Push a compiled chunk as a closure onto the stack.
 */
SPT_API void spt_pushchunk(spt_State *S, spt_Chunk *chunk);

/* ============================================================================
 * 15. EXECUTION
 * ============================================================================ */

/*
 * Execute a compiled chunk.
 * Equivalent to pushing it and calling with 0 args.
 */
SPT_API int spt_execute(spt_State *S, spt_Chunk *chunk);

/*
 * Call a function on the stack.
 * Stack: [..., func, arg1, ..., argN] -> spt_call(S, nargs, nresults) -> [..., res1, ..., resN]
 * Use SPT_MULTRET for nresults to keep all results.
 */
SPT_API int spt_call(spt_State *S, int nargs, int nresults);

/*
 * Protected call (catches errors).
 * Stack: [..., func, arg1, ..., argN] -> spt_pcall(...) -> [..., res1, ...] or [..., errmsg]
 * errfunc: stack index of error handler (0 for none)
 * Returns SPT_OK or error code.
 */
SPT_API int spt_pcall(spt_State *S, int nargs, int nresults, int errfunc);

/*
 * Call a method on an object.
 * Stack: [..., object, arg1, ..., argN] -> spt_callmethod(S, "method", nargs, nresults)
 *     -> [..., res1, ..., resN]
 */
SPT_API int spt_callmethod(spt_State *S, const char *method, int nargs, int nresults);

/*
 * Protected method call.
 */
SPT_API int spt_pcallmethod(spt_State *S, const char *method, int nargs, int nresults, int errfunc);

/*
 * Execute a string directly (parse + compile + execute).
 * Returns status code.
 */
SPT_API int spt_dostring(spt_State *S, const char *source, const char *name);

/*
 * Execute a file directly.
 */
SPT_API int spt_dofile(spt_State *S, const char *filename);

/* ============================================================================
 * 16. FIBER (COROUTINE)
 * ============================================================================ */

/*
 * Create a new fiber from a function at stack top.
 * Stack: [..., func] -> spt_newfiber(S) -> [..., fiber]
 * Returns the fiber state handle.
 */
SPT_API spt_State *spt_newfiber(spt_State *S);

/*
 * Resume a fiber.
 * Stack on 'from': [..., arg1, ..., argN]
 * Returns status (SPT_OK, SPT_YIELD, or error).
 * Results are left on 'from' stack.
 */
SPT_API int spt_resume(spt_State *S, spt_State *from, int nargs);

/*
 * Yield from the current fiber.
 * Stack: [..., res1, ..., resN] -> spt_yield(S, nresults)
 * Returns number of values passed back on resume.
 */
SPT_API int spt_yield(spt_State *S, int nresults);

/*
 * Get fiber status.
 * Returns one of SPT_FIBER_* constants.
 */
SPT_API int spt_fiberstatus(spt_State *S);

/*
 * Check if fiber can be resumed.
 */
SPT_API int spt_isresumable(spt_State *S);

/*
 * Abort a fiber with an error.
 * Stack: [..., error] -> spt_fiberabort(S) -> [...]
 */
SPT_API void spt_fiberabort(spt_State *S);

/*
 * Get fiber's error value (if in ERROR state).
 * Pushes the error onto stack.
 */
SPT_API void spt_fibererror(spt_State *S);

/* ============================================================================
 * 17. GLOBALS & REGISTRY
 * ============================================================================ */

/*
 * Push the global with the given name.
 * Returns the type of the pushed value.
 */
SPT_API int spt_getglobal(spt_State *S, const char *name);

/*
 * Set global[name] = top value, pop value.
 */
SPT_API void spt_setglobal(spt_State *S, const char *name);

/*
 * Check if a global exists.
 */
SPT_API int spt_hasglobal(spt_State *S, const char *name);

/*
 * Create a reference to the value at stack top.
 * Returns a reference ID. Pop the value.
 */
SPT_API int spt_ref(spt_State *S);

/*
 * Release a reference.
 */
SPT_API void spt_unref(spt_State *S, int ref);

/*
 * Push the value associated with a reference.
 */
SPT_API void spt_getref(spt_State *S, int ref);

/* ============================================================================
 * 18. MODULE SYSTEM
 * ============================================================================ */

/*
 * Add a path to the module search paths.
 */
SPT_API void spt_addpath(spt_State *S, const char *path);

/*
 * Import a module.
 * Pushes the module's exports table.
 * Returns status code.
 */
SPT_API int spt_import(spt_State *S, const char *name);

/*
 * Reload a module (hot reload).
 * Returns status code.
 */
SPT_API int spt_reload(spt_State *S, const char *name);

/*
 * Register a C module with the given functions.
 * After registration, the module can be imported by name.
 */
SPT_API void spt_defmodule(spt_State *S, const char *name, const spt_Reg *funcs);

/*
 * Tick modules for hot reload checking.
 * Call periodically if hot reload is enabled.
 */
SPT_API void spt_tickmodules(spt_State *S);

/*
 * Pre-register a compiled chunk as a module.
 */
SPT_API void spt_registermodule(spt_State *S, const char *name, spt_Chunk *chunk);

/* ============================================================================
 * 19. ERROR HANDLING
 * ============================================================================ */

/*
 * Raise an error with a formatted message.
 * This function does not return.
 */
SPT_API void spt_error(spt_State *S, const char *fmt, ...);

/*
 * Raise an error with the value at stack top.
 * Stack: [..., errorval] -> spt_throw(S) -> (does not return)
 */
SPT_API void spt_throw(spt_State *S);

/*
 * Set the error handler callback.
 */
SPT_API void spt_seterrorhandler(spt_State *S, spt_ErrorHandler handler, void *ud);

/*
 * Set the print handler callback (for print() function).
 */
SPT_API void spt_setprinthandler(spt_State *S, spt_PrintHandler handler, void *ud);

/*
 * Get the last error message.
 * Returns NULL if no error.
 */
SPT_API const char *spt_getlasterror(spt_State *S);

/*
 * Get the current stack trace as a string.
 * Pushes the string onto the stack.
 */
SPT_API void spt_stacktrace(spt_State *S);

/*
 * Get debug information about a function.
 * func_idx: stack index of the function
 * what: string of options:
 *   'S' - source info (name, source, lineDefined)
 *   'l' - current line
 * Returns 1 on success.
 */
SPT_API int spt_getinfo(spt_State *S, int func_idx, const char *what, const char **name,
                        const char **source, int *lineDefined, int *currentLine);

/*
 * Get debug info about a stack frame.
 * level: 0 = current function, 1 = caller, etc.
 */
SPT_API int spt_getstack(spt_State *S, int level, const char *what, const char **name,
                         const char **source, int *lineDefined, int *currentLine);

/* ============================================================================
 * 20. GARBAGE COLLECTION
 * ============================================================================ */

enum {
  SPT_GCSTOP = 0,       /* Stop GC */
  SPT_GCRESTART = 1,    /* Restart GC */
  SPT_GCCOLLECT = 2,    /* Force full collection */
  SPT_GCCOUNT = 3,      /* Get memory in KB */
  SPT_GCCOUNTB = 4,     /* Get memory in bytes */
  SPT_GCSTEP = 5,       /* Incremental step */
  SPT_GCSETPAUSE = 6,   /* Set pause (not implemented) */
  SPT_GCSETSTEPMUL = 7, /* Set step multiplier (not implemented) */
  SPT_GCISRUNNING = 8,  /* Check if GC is running */
  SPT_GCOBJCOUNT = 9    /* Get object count */
};

/*
 * Control the garbage collector.
 * Returns result depending on 'what'.
 */
SPT_API int spt_gc(spt_State *S, int what, int data);

/* ============================================================================
 * 21. LIBRARY REGISTRATION
 * ============================================================================ */

/*
 * Register a list of functions.
 * If libname is NULL, registers as globals.
 * If libname is provided, creates/uses a table and sets it as global.
 */
SPT_API void spt_register(spt_State *S, const char *libname, const spt_Reg *funcs);

/*
 * Register methods to a class.
 * class_idx: stack index of the class.
 */
SPT_API void spt_registermethods(spt_State *S, int class_idx, const spt_MethodReg *methods);

/*
 * Open standard libraries.
 */
SPT_API void spt_openlibs(spt_State *S);

/* ============================================================================
 * 22. UTILITY FUNCTIONS
 * ============================================================================ */

/*
 * Get the length of a value (works for strings, lists, maps).
 */
SPT_API size_t spt_len(spt_State *S, int idx);

/*
 * Concatenate n values from stack top into a string.
 * Stack: [..., v1, v2, ..., vN] -> spt_concat(S, n) -> [..., "v1v2...vN"]
 */
SPT_API void spt_concat(spt_State *S, int n);

/*
 * Convert value to string representation (for debugging).
 * Pushes the string onto stack.
 */
SPT_API void spt_tostr(spt_State *S, int idx);

/*
 * Intern a string (get canonical pointer).
 * Returns the interned string pointer.
 */
SPT_API const char *spt_internstring(spt_State *S, const char *str, size_t len);

/*
 * Argument checking helpers for C functions.
 */
SPT_API void spt_argcheck(spt_State *S, int cond, int arg, const char *msg);
SPT_API int spt_argerror(spt_State *S, int arg, const char *msg);
SPT_API int spt_typeerror(spt_State *S, int arg, const char *tname);

SPT_API spt_Int spt_checkint(spt_State *S, int arg);
SPT_API spt_Float spt_checkfloat(spt_State *S, int arg);
SPT_API const char *spt_checkstring(spt_State *S, int arg, size_t *len);
SPT_API void spt_checktype(spt_State *S, int arg, int tp);
SPT_API void spt_checkany(spt_State *S, int arg);

SPT_API spt_Int spt_optint(spt_State *S, int arg, spt_Int def);
SPT_API spt_Float spt_optfloat(spt_State *S, int arg, spt_Float def);
SPT_API const char *spt_optstring(spt_State *S, int arg, const char *def);

SPT_API void *spt_checklightuserdata(spt_State *S, int arg);
SPT_API void *spt_optlightuserdata(spt_State *S, int arg, void *def);

/* ============================================================================
 * 23. ITERATION SUPPORT
 * ============================================================================ */

/*
 * Begin iteration over a list.
 * Returns iterator state or -1 if not a list.
 */
SPT_API int spt_listiter(spt_State *S, int idx);

/*
 * Get next element in list iteration.
 * Stack: [...] -> spt_listnext(S, listidx, &iter) -> [..., value] or no push if done
 * Returns 1 if value pushed, 0 if iteration complete.
 */
SPT_API int spt_listnext(spt_State *S, int idx, int *iter);

/*
 * Generic pairs iteration for maps.
 * Call spt_mapnext() with nil key to start.
 */

/* ============================================================================
 * 24. VERSION INFORMATION
 * ============================================================================ */

/*
 * Get version string.
 */
SPT_API const char *spt_version(void);

/*
 * Get version number.
 */
SPT_API int spt_versionnum(void);

#ifdef __cplusplus
}
#endif

#endif /* SPT_H */
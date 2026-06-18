/*
 * examples/run_source.c — Compile and run real SPT source text.
 *
 * This drives the full frontend: spt_load() lexes, parses, and compiles the
 * source string to bytecode and leaves a callable closure on the stack, which
 * we then run. A small `print` host function lets the scripts produce output.
 */
#include "spt.h"
#include <stdio.h>

/* print(x): host function. Slot-0 aware — first real argument is arg 1. */
static int host_print(spt_State *L) {
  int n = spt_gettop(L) - 1;            /* exclude the receiver at index 1 */
  for (int i = 1; i <= n; i++) {
    if (i > 1) printf("\t");
    int idx = i + 1;                     /* arg i is at stack index i+1 */
    if (spt_isnull(L, idx)) { printf("null"); continue; }
    if (spt_isbool(L, idx)) { printf(spt_tobool(L, idx) ? "true" : "false"); continue; }
    const char *s = spt_tostring(L, idx);
    if (s) printf("%s", s);
    else {
      double d = spt_tofloat(L, idx);
      if (d == (double)(long long)d) printf("%lld", (long long)d);
      else printf("%g", d);
    }
  }
  printf("\n");
  return 0;
}

static void run(spt_State *L, const char *title, const char *src) {
  printf("--- %s ---\n", src);
  if (spt_load(L, src, title) != 0) {
    printf("compile error: %s\n\n", L->errmsg ? L->errmsg : "?");
    return;
  }
  setnull(L->top); L->top++;             /* receiver for the chunk call */
  if (spt_call(L, 0, 0) != 0)
    printf("runtime error: %s\n", L->errmsg ? L->errmsg : "?");
  L->top = L->stack;                     /* reset */
  printf("\n");
}

/* Show the static type checker rejecting a program before it ever runs. */
static void expect_reject(spt_State *L, const char *src) {
  printf("--- %s ---\n", src);
  if (spt_load(L, src, "check") != 0)
    printf("  rejected at compile time: %s\n\n", L->errmsg ? L->errmsg : "?");
  else { printf("  (unexpectedly accepted)\n\n"); L->top = L->stack; }
}

#ifdef SPT_HAS_JIT
/* Call the global function `fact` with integer n and return its result. */
static long long call_fact(spt_State *L, long long n) {
  L->top = L->stack;
  spt_getglobal(L, "fact");
  setnull(L->top); L->top++;
  spt_pushint(L, n);
  spt_call(L, 1, 1);
  long long r = spt_toint(L, 1);
  L->top = L->stack;
  return r;
}

/* The headline: a *typed* SPT source function compiled to native code.
 * Because its locals/params are typed, the codegen emits OP_IMUL/OP_IADD —
 * exactly the opcodes the JIT lowers — so this whole loop becomes native. */
static void demo_jit_from_source(spt_State *L) {
  const char *src =
    "function fact(int n) {\n"
    "  int acc = 1;\n"
    "  int i = 1;\n"
    "  while (i <= n) { acc = acc * i; i = i + 1; }\n"
    "  return acc;\n"
    "}\n";
  if (spt_load(L, src, "fact") != 0) { printf("compile error: %s\n", L->errmsg); return; }
  setnull(L->top); L->top++;
  spt_call(L, 0, 0);                     /* run chunk: binds global fact */
  L->top = L->stack;

  long long interp = call_fact(L, 12);

  spt_getglobal(L, "fact");
  Proto *p = clvalue(L->top - 1)->p;     /* the compiled prototype */
  L->top = L->stack;
  spt_jit_try_compile(L, p);             /* force native compilation */
  int compiled = (p->jit_entry != NULL);

  long long native = call_fact(L, 12);

  printf("--- typed source -> JIT ---\n");
  printf("  fact(12): interp=%lld  native=%lld  [%s]\n\n",
         interp, native,
         compiled ? (interp == native ? "compiled to native, results match"
                                      : "MISMATCH")
                  : "not compiled (bailed)");
}

/* Exercise the expanded JIT coverage: integer division/modulo and float
 * arithmetic.  All operands are typed so codegen emits OP_IDIV/OP_IMOD/
 * OP_FADD/OP_FMUL/OP_FDIV — exactly the opcodes the JIT now lowers. */
static void demo_jit_arith(spt_State *L) {
  const char *src =
    "function intmath(int n) {\n"
    "  int sum = 0;\n"
    "  int i = 1;\n"
    "  while (i <= n) { sum = sum + i * i; i = i + 1; }\n"
    "  int q = sum / 7;\n"
    "  int r = sum % 7;\n"
    "  return q * 100 + r;\n"
    "}\n"
    "function floatmath(float x) {\n"
    "  float pi = 3.14;\n"
    "  float area = pi * x * x;\n"
    "  float half = area / 2.0;\n"
    "  return half;\n"
    "}\n";
  if (spt_load(L, src, "jit-arith") != 0) { printf("compile error: %s\n", L->errmsg); return; }
  setnull(L->top); L->top++;
  spt_call(L, 0, 0);
  L->top = L->stack;

  /* intmath: interp vs native */
  spt_getglobal(L, "intmath");
  Proto *pi = clvalue(L->top - 1)->p;
  L->top = L->stack;

  spt_getglobal(L, "intmath"); setnull(L->top); L->top++; spt_pushint(L, 100);
  spt_call(L, 1, 1);
  long long i_interp = spt_toint(L, 1);
  L->top = L->stack;

  spt_jit_try_compile(L, pi);
  int i_compiled = (pi->jit_entry != NULL);

  spt_getglobal(L, "intmath"); setnull(L->top); L->top++; spt_pushint(L, 100);
  spt_call(L, 1, 1);
  long long i_native = spt_toint(L, 1);
  L->top = L->stack;

  printf("--- JIT int arithmetic (IDIV, IMOD) ---\n");
  printf("  intmath(100): interp=%lld  native=%lld  [%s]\n\n",
         i_interp, i_native,
         !i_compiled ? "not compiled (bailed)"
                     : (i_interp == i_native ? "compiled, results match" : "MISMATCH"));

  /* floatmath: interp vs native */
  spt_getglobal(L, "floatmath");
  Proto *pf = clvalue(L->top - 1)->p;
  L->top = L->stack;

  spt_getglobal(L, "floatmath"); setnull(L->top); L->top++; spt_pushfloat(L, 5.0);
  spt_call(L, 1, 1);
  double f_interp = spt_tofloat(L, 1);
  L->top = L->stack;

  spt_jit_try_compile(L, pf);
  int f_compiled = (pf->jit_entry != NULL);

  spt_getglobal(L, "floatmath"); setnull(L->top); L->top++; spt_pushfloat(L, 5.0);
  spt_call(L, 1, 1);
  double f_native = spt_tofloat(L, 1);
  L->top = L->stack;

  printf("--- JIT float arithmetic (FADD, FMUL, FDIV) ---\n");
  printf("  floatmath(5.0): interp=%.6f  native=%.6f  [%s]\n\n",
         f_interp, f_native,
         !f_compiled ? "not compiled (bailed)"
                     : (f_interp == f_native ? "compiled, results match" : "MISMATCH"));
}

/* Exercise OP_NEG, OP_NOT, OP_CAST through the JIT. */
static void demo_jit_unary(spt_State *L) {
  const char *src =
    "function negtest(int n) {\n"
    "  int x = -n;\n"
    "  return x + n;\n"              /* -n + n == 0 */
    "}\n"
    "function casttest(float f) {\n"
    "  int n = (int)f;\n"
    "  float g = (float)n;\n"
    "  return (int)(g + 0.5);\n"     /* round to nearest */
    "}\n";
  if (spt_load(L, src, "jit-unary") != 0) { printf("compile error: %s\n", L->errmsg); return; }
  setnull(L->top); L->top++;
  spt_call(L, 0, 0);
  L->top = L->stack;

  /* negtest: interp vs native */
  spt_getglobal(L, "negtest");
  Proto *pn = clvalue(L->top - 1)->p;
  L->top = L->stack;

  spt_getglobal(L, "negtest"); setnull(L->top); L->top++; spt_pushint(L, 42);
  spt_call(L, 1, 1);
  long long n_interp = spt_toint(L, 1);
  L->top = L->stack;

  spt_jit_try_compile(L, pn);
  int n_compiled = (pn->jit_entry != NULL);

  spt_getglobal(L, "negtest"); setnull(L->top); L->top++; spt_pushint(L, 42);
  spt_call(L, 1, 1);
  long long n_native = spt_toint(L, 1);
  L->top = L->stack;

  printf("--- JIT unary (NEG) ---\n");
  printf("  negtest(42): interp=%lld  native=%lld  [%s]\n\n",
         n_interp, n_native,
         !n_compiled ? "not compiled (bailed)"
                     : (n_interp == n_native ? "compiled, results match" : "MISMATCH"));

  /* casttest: interp vs native */
  spt_getglobal(L, "casttest");
  Proto *pc = clvalue(L->top - 1)->p;
  L->top = L->stack;

  spt_getglobal(L, "casttest"); setnull(L->top); L->top++; spt_pushfloat(L, 3.7);
  spt_call(L, 1, 1);
  long long c_interp = spt_toint(L, 1);
  L->top = L->stack;

  spt_jit_try_compile(L, pc);
  int c_compiled = (pc->jit_entry != NULL);

  spt_getglobal(L, "casttest"); setnull(L->top); L->top++; spt_pushfloat(L, 3.7);
  spt_call(L, 1, 1);
  long long c_native = spt_toint(L, 1);
  L->top = L->stack;

  printf("--- JIT cast (CAST) ---\n");
  printf("  casttest(3.7): interp=%lld  native=%lld  [%s]\n\n",
         c_interp, c_native,
         !c_compiled ? "not compiled (bailed)"
                     : (c_interp == c_native ? "compiled, results match" : "MISMATCH"));
}

static void demo_jit_concat(spt_State *L) {
  const char *src =
    "function greet(string name, int n) {\n"
    "  string s = \"Hello, \" .. name .. \"! You are \" .. n .. \" years old.\";\n"
    "  return s;\n"
    "}\n";
  if (spt_load(L, src, "jit-concat") != 0) { printf("compile error: %s\n", L->errmsg); return; }
  setnull(L->top); L->top++;
  spt_call(L, 0, 0);
  L->top = L->stack;

  spt_getglobal(L, "greet");
  Proto *pc = clvalue(L->top - 1)->p;
  L->top = L->stack;

  spt_getglobal(L, "greet"); setnull(L->top); L->top++;
  spt_pushstring(L, "Alice"); spt_pushint(L, 30);
  spt_call(L, 2, 1);
  const char *s_interp = spt_tostring(L, 1);
  L->top = L->stack;

  spt_jit_try_compile(L, pc);
  int compiled = (pc->jit_entry != NULL);

  spt_getglobal(L, "greet"); setnull(L->top); L->top++;
  spt_pushstring(L, "Alice"); spt_pushint(L, 30);
  spt_call(L, 2, 1);
  const char *s_native = spt_tostring(L, 1);
  L->top = L->stack;

  printf("--- JIT concat (CONCAT) ---\n");
  printf("  greet(\"Alice\",30): interp=\"%s\"  native=\"%s\"  [%s]\n\n",
         s_interp ? s_interp : "(null)",
         s_native ? s_native : "(null)",
         !compiled ? "not compiled (bailed)"
                   : (s_interp && s_native && strcmp(s_interp, s_native) == 0
                      ? "compiled, results match" : "MISMATCH"));
}

/* Exercise globals, upvalues, indexing, lists, maps, generic arith, len —
 * all the newly JIT-supported opcodes — in one function. */
static void demo_jit_mixed(spt_State *L) {
  const char *src =
    "global counter = 42;\n"
    "function make_acc() {\n"
    "  int total = 0;\n"
    "  function add(int x) { total = total + x; return total; }\n"
    "  return add;\n"
    "}\n"
    "function sum_list(xs) {\n"
    "  s = 0;\n"
    "  int i = 0;\n"
    "  while (i < #xs) { s = s + xs[i]; i = i + 1; }\n"
    "  return s;\n"
    "}\n"
    "function build_and_sum() {\n"
    "  xs = [10, 20, 30, 40, 50];\n"
    "  return sum_list(xs);\n"
    "}\n"
    "function dyn_arith(a, b) {\n"
    "  return a + b * 2 - 1;\n"
    "}\n"
    "function read_global() {\n"
    "  return counter;\n"
    "}\n";
  if (spt_load(L, src, "jit-mixed") != 0) { printf("compile error: %s\n", L->errmsg); return; }
  setnull(L->top); L->top++;
  spt_call(L, 0, 0);
  L->top = L->stack;

  /* --- test upvalues (GETUPVAL/SETUPVAL) ---
   * make_acc itself bails (it contains OP_CLOSURE), but the inner `add`
   * function only uses OP_GETUPVAL/OP_SETUPVAL/OP_IADD/OP_RETURN — all
   * JIT-supported.  We force-compile `add`'s proto and verify it runs
   * correctly when called through the interpreter. */
  spt_getglobal(L, "make_acc");
  Proto *p_make = clvalue(L->top - 1)->p;
  Proto *p_add  = p_make->p[0];             /* the nested `add` function */
  L->top = L->stack;

  /* interpreter baseline: call make_acc() then add(10), add(20) */
  spt_getglobal(L, "make_acc"); setnull(L->top); L->top++;
  spt_call(L, 0, 1);                         /* stack[1] = add closure   */
  spt_pushvalue(L, 1); setnull(L->top); L->top++; spt_pushint(L, 10);
  spt_call(L, 1, 1);                         /* stack[2] = 10            */
  long long acc1 = spt_toint(L, 2);
  spt_pushvalue(L, 1); setnull(L->top); L->top++; spt_pushint(L, 20);
  spt_call(L, 1, 1);                         /* stack[3] = 30            */
  long long acc2 = spt_toint(L, 3);
  L->top = L->stack;

  spt_jit_try_compile(L, p_add);
  int add_compiled = (p_add->jit_entry != NULL);

  /* native run: add proto is now JIT-compiled */
  spt_getglobal(L, "make_acc"); setnull(L->top); L->top++;
  spt_call(L, 0, 1);
  spt_pushvalue(L, 1); setnull(L->top); L->top++; spt_pushint(L, 10);
  spt_call(L, 1, 1);
  long long acc1n = spt_toint(L, 2);
  spt_pushvalue(L, 1); setnull(L->top); L->top++; spt_pushint(L, 20);
  spt_call(L, 1, 1);
  long long acc2n = spt_toint(L, 3);
  L->top = L->stack;

  printf("--- JIT upvalues (GETUPVAL/SETUPVAL) ---\n");
  printf("  make_acc/add: interp=%lld,%lld  native=%lld,%lld  [%s]\n\n",
         acc1, acc2, acc1n, acc2n,
         !add_compiled ? "not compiled (bailed)"
                       : (acc1 == acc1n && acc2 == acc2n ? "compiled, results match" : "MISMATCH"));

  /* --- test build_and_sum (NEWLIST/LISTPUSH + GETINDEX/LEN + CALL) ---
   * Note: build_and_sum calls sum_list, which uses OP_CALL. Since OP_CALL
   * is not yet JIT-supported, build_and_sum will bail. But sum_list itself
   * (when called directly with a list arg) can be JIT-compiled. */
  spt_getglobal(L, "sum_list");
  Proto *p_sl = clvalue(L->top - 1)->p;
  L->top = L->stack;

  /* call build_and_sum to get the reference result (uses interpreter) */
  spt_getglobal(L, "build_and_sum"); setnull(L->top); L->top++;
  spt_call(L, 0, 1);
  long long sl_interp = spt_toint(L, 1);
  L->top = L->stack;

  spt_jit_try_compile(L, p_sl);
  int sl_compiled = (p_sl->jit_entry != NULL);

  /* call build_and_sum again — sum_list will run JIT-compiled when called */
  spt_getglobal(L, "build_and_sum"); setnull(L->top); L->top++;
  spt_call(L, 0, 1);
  long long sl_native = spt_toint(L, 1);
  L->top = L->stack;

  printf("--- JIT list+index+len (GETINDEX/LEN/NEWLIST/LISTPUSH) ---\n");
  printf("  sum_list([10..50]): interp=%lld  native=%lld  [%s]\n\n",
         sl_interp, sl_native,
         !sl_compiled ? "not compiled (bailed)"
                      : (sl_interp == sl_native ? "compiled, results match" : "MISMATCH"));

  /* --- test dyn_arith (generic ADD/SUB/MUL) --- */
  spt_getglobal(L, "dyn_arith");
  Proto *p_da = clvalue(L->top - 1)->p;
  L->top = L->stack;

  spt_getglobal(L, "dyn_arith"); setnull(L->top); L->top++;
  spt_pushint(L, 3); spt_pushint(L, 4);
  spt_call(L, 2, 1);
  long long da_interp = spt_toint(L, 1);
  L->top = L->stack;

  spt_jit_try_compile(L, p_da);
  int da_compiled = (p_da->jit_entry != NULL);

  spt_getglobal(L, "dyn_arith"); setnull(L->top); L->top++;
  spt_pushint(L, 3); spt_pushint(L, 4);
  spt_call(L, 2, 1);
  long long da_native = spt_toint(L, 1);
  L->top = L->stack;

  printf("--- JIT generic arith (ADD/SUB/MUL) ---\n");
  printf("  dyn_arith(3,4): interp=%lld  native=%lld  [%s]\n\n",
         da_interp, da_native,
         !da_compiled ? "not compiled (bailed)"
                      : (da_interp == da_native ? "compiled, results match" : "MISMATCH"));

  /* --- test globals (GETGLOBAL) --- */
  spt_getglobal(L, "read_global");
  Proto *p_rg = clvalue(L->top - 1)->p;
  L->top = L->stack;

  spt_getglobal(L, "read_global"); setnull(L->top); L->top++;
  spt_call(L, 0, 1);
  long long g_interp = spt_toint(L, 1);
  L->top = L->stack;

  spt_jit_try_compile(L, p_rg);
  int g_compiled = (p_rg->jit_entry != NULL);

  spt_getglobal(L, "read_global"); setnull(L->top); L->top++;
  spt_call(L, 0, 1);
  long long g_native = spt_toint(L, 1);
  L->top = L->stack;

  printf("--- JIT globals (GETGLOBAL) ---\n");
  printf("  read_global(): interp=%lld  native=%lld  [%s]\n\n",
         g_interp, g_native,
         !g_compiled ? "not compiled (bailed)"
                     : (g_interp == g_native ? "compiled, results match" : "MISMATCH"));
}
#endif

int main(void) {
  spt_State *L = spt_newstate();
  spt_register(L, "print", host_print);

  printf("== SPT source demo ==\n\n");

  run(L, "arith", "print((2 + 3) * 4 - 10 / 2);");

  run(L, "variables",
      "x = 21;\n"
      "y = x * 2;\n"
      "print(y);");

  run(L, "recursion",
      "function fib(n) {\n"
      "  if (n < 2) { return n; }\n"
      "  return fib(n - 1) + fib(n - 2);\n"
      "}\n"
      "print(fib(20));");

  run(L, "while-loop",
      "function sum(n) {\n"
      "  s = 0;\n"
      "  i = 1;\n"
      "  while (i <= n) { s = s + i; i = i + 1; }\n"
      "  return s;\n"
      "}\n"
      "print(sum(100));");

  run(L, "lists",
      "xs = [10, 20, 30, 40];\n"
      "xs[1] = 99;\n"
      "print(xs[0] + xs[1] + xs[3]);");

  run(L, "strings-and-conditionals",
      "function classify(n) {\n"
      "  if (n % 2 == 0) { return \"even\"; }\n"
      "  return \"odd\";\n"
      "}\n"
      "print(classify(7));\n"
      "print(classify(10));");

  printf("-- closures (upvalue capture) --\n\n");
  run(L, "counter-factory",
      "function make_counter() {\n"
      "  count = 0;\n"
      "  function inc() { count = count + 1; return count; }\n"
      "  return inc;\n"
      "}\n"
      "c1 = make_counter();\n"
      "c2 = make_counter();\n"
      "print(c1());\n"          /* 1 */
      "print(c1());\n"          /* 2 */
      "print(c1());\n"          /* 3 */
      "print(c2());");          /* 1 — independent capture */
  run(L, "adder-factory",
      "function adder(int n) {\n"
      "  function add(int x) { return x + n; }\n"   /* captures typed n */
      "  return add;\n"
      "}\n"
      "add5 = adder(5);\n"
      "add100 = adder(100);\n"
      "print(add5(10));\n"      /* 15 */
      "print(add100(10));");    /* 110 */

  printf("-- static type checking --\n\n");
  run(L, "typed-arithmetic",
      "int a = 6;\n"
      "int b = 7;\n"
      "print(a * b);");
  expect_reject(L, "int x = \"hello\";");
  expect_reject(L, "const int K = 5;\nK = 6;");

  printf("-- explicit type conversion (cast) --\n\n");
  /* numeric casts: int <-> float */
  run(L, "int-to-float",
      "int n = 42;\n"
      "float f = (float)n;\n"
      "print(f);");
  run(L, "float-to-int",
      "float pi = 3.99;\n"
      "int whole = (int)pi;\n"
      "print(whole);");
  /* string casts */
  run(L, "int-to-string",
      "int n = 123;\n"
      "string s = (string)n;\n"
      "print(s);");
  run(L, "string-to-int",
      "string s = \"456\";\n"
      "int n = (int)s;\n"
      "print(n + 1);");
  run(L, "float-to-string",
      "float x = 3.14;\n"
      "print((string)x);");
  run(L, "string-to-float",
      "string s = \"2.5\";\n"
      "float f = (float)s;\n"
      "print(f + 1.0);");
  /* bool casts */
  run(L, "int-to-bool",
      "print((bool)0);\n"
      "print((bool)7);");
  run(L, "bool-to-int",
      "print((int)true);\n"
      "print((int)false);");
  /* the key use case: crossing the dynamic→typed boundary */
  run(L, "dynamic-to-typed",
      "function get_value() { return 42; }\n"
      "int n = (int)get_value();\n"
      "print(n * 2);");
  run(L, "cast-in-expression",
      "int a = 7;\n"
      "float b = 2.0;\n"
      "print((int)a + (int)b);");
  /* cast enables sound typing: assign dynamic to typed via explicit cast */
  run(L, "typed-var-from-dynamic",
      "dyn = 100;\n"
      "int typed = (int)dyn;\n"
      "print(typed + 1);");

  run(L, "string-concat",
      "string s = \"Hello\" .. \" \" .. \"World\";\n"
      "print(s);");

  run(L, "concat-with-coercion",
      "int n = 42;\n"
      "print(\"answer = \" .. n);");

  run(L, "break-in-loop",
      "int i = 0;\n"
      "int sum = 0;\n"
      "while (i < 100) {\n"
      "  if (i == 10) { break; }\n"
      "  sum = sum + i;\n"
      "  i = i + 1;\n"
      "}\n"
      "print(sum);");

  run(L, "continue-in-loop",
      "int i = 0;\n"
      "int sum = 0;\n"
      "while (i < 10) {\n"
      "  i = i + 1;\n"
      "  if (i == 3) { continue; }\n"
      "  if (i == 7) { continue; }\n"
      "  sum = sum + i;\n"
      "}\n"
      "print(sum);");

  run(L, "break-and-continue",
      "int i = 0;\n"
      "int count = 0;\n"
      "while (i < 50) {\n"
      "  i = i + 1;\n"
      "  if (i == 5) { continue; }\n"
      "  if (i == 8) { break; }\n"
      "  count = count + 1;\n"
      "}\n"
      "print(count);");

#ifdef SPT_HAS_JIT
  printf("-- typed source through the JIT --\n\n");
  demo_jit_from_source(L);
  demo_jit_arith(L);
  demo_jit_unary(L);
  demo_jit_concat(L);
  demo_jit_mixed(L);
#endif

  printf("== done ==\n");
  spt_close(L);
  return 0;
}

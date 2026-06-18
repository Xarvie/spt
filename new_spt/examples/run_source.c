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

#ifdef SPT_HAS_JIT
  printf("-- typed source through the JIT --\n\n");
  demo_jit_from_source(L);
#endif

  printf("== done ==\n");
  spt_close(L);
  return 0;
}

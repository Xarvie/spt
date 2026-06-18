/*
 * examples/demo.c — A guided tour that proves the core runs end to end.
 *
 * Each demo hand-assembles a small bytecode prototype (the frontend/codegen
 * that will emit these is a later milestone), runs it through the interpreter,
 * and reads the result back through the public C API.
 */
#include "spt.h"
#include <stdio.h>
#include <string.h>

/* ---- tiny prototype-assembly helpers ---- */
static Proto *make_proto(spt_State *L, const Instr *code, int ncode,
                         int numparams, int maxstack) {
  Proto *p = spt_proto_new(L);
  p->ncode = (uint32_t)ncode;
  p->code = (Instr *)spt_alloc(L, (size_t)ncode * sizeof(Instr));
  memcpy(p->code, code, (size_t)ncode * sizeof(Instr));
  p->numparams = (uint8_t)numparams;
  p->maxstack = (uint8_t)maxstack;
  return p;
}

static void set_string_consts(spt_State *L, Proto *p, const char **strs, int n) {
  p->nk = (uint32_t)n;
  p->k = (TValue *)spt_alloc(L, (size_t)n * sizeof(TValue));
  for (int i = 0; i < n; i++) {
    String *s = spt_str_new(L, strs[i]);
    setgco(&p->k[i], (GCObject *)s, SPT_TSTRING);
  }
}

/* Push a chunk closure + nil receiver and run it, leaving `nresults` on stack. */
static int run_chunk(spt_State *L, Proto *p, int nresults) {
  Closure *cl = spt_closure_new(L, p);
  spt_checkstack(L, nresults + 4);
  setgco(L->top, (GCObject *)cl, SPT_TCLOSURE); L->top++;   /* callable  */
  setnull(L->top); L->top++;                                /* receiver  */
  return spt_call(L, 0, nresults);
}

static void reset(spt_State *L) { L->top = L->stack; }

#ifdef SPT_HAS_JIT
/* Call a 1-argument SPT closure `cl` with integer argument `n`, return result. */
static long long call1(spt_State *L, Closure *cl, long long n) {
  spt_checkstack(L, 8);
  setgco(L->top, (GCObject *)cl, SPT_TCLOSURE); L->top++;   /* callable */
  setnull(L->top); L->top++;                                /* receiver */
  spt_pushint(L, n);                                        /* argument */
  spt_call(L, 1, 1);
  long long r = spt_toint(L, 1);
  reset(L);
  return r;
}
#endif

/* ---- a host (C) function callable from bytecode ---- */
/* Slot-0 aware: arg 1 and arg 2 are the first two real arguments. */
static int c_add(spt_State *L) {
  spt_pushint(L, spt_arg_int(L, 1) + spt_arg_int(L, 2));
  return 1;
}

#ifdef SPT_HAS_JIT
/* Compile a hot prototype to native code and check it agrees with the
 * interpreter. The body is an iterative factorial — a real loop with a
 * back-edge, a multiply, and a comparison — exercising the lowering end to end. */
static void demo_jit(spt_State *L) {
  /* int fact(int n) { acc=1; for (i=1; i<=n; i++) acc*=i; return acc; }
   * R0=receiver, R1=n, R2=acc, R3=i, R4=cond, R5=tmp                         */
  const Instr code[] = {
    SPT_MK_AsBx(OP_LOADINT, 2, 1),     /* 0: acc = 1            */
    SPT_MK_AsBx(OP_LOADINT, 3, 1),     /* 1: i   = 1            */
    SPT_MK_ABC (OP_LE,      4, 3, 1),  /* 2: R4 = (i <= n)      */
    SPT_MK_ABC (OP_TEST,    4, 0, 0),  /* 3: if !R4 skip next   */
    SPT_MK_AsBx(OP_JMP,     0, 4),     /* 4: -> end (idx 9)     */
    SPT_MK_ABC (OP_IMUL,    2, 2, 3),  /* 5: acc *= i           */
    SPT_MK_AsBx(OP_LOADINT, 5, 1),     /* 6: tmp = 1            */
    SPT_MK_ABC (OP_IADD,    3, 3, 5),  /* 7: i += 1             */
    SPT_MK_AsBx(OP_JMP,     0, -7),    /* 8: -> loop (idx 2)    */
    SPT_MK_ABC (OP_RETURN,  2, 2, 0),  /* 9: return acc         */
  };
  Proto *fact = make_proto(L, code, (int)(sizeof(code) / sizeof(code[0])), 1, 6);

  const long long n = 12;
  long long interp = call1(L, spt_closure_new(L, fact), n);   /* interpreter path */

  spt_jit_try_compile(L, fact);                               /* force native compile */
  int compiled = (fact->jit_entry != NULL);

  long long native = call1(L, spt_closure_new(L, fact), n);   /* native path */

  printf("7. jit         factorial(%lld): interp=%lld  native=%lld  [%s]\n",
         n, interp, native,
         !compiled ? "not compiled (bailed)"
                   : (interp == native ? "compiled to native, results match"
                                       : "MISMATCH"));
}
#endif

int main(void) {
  spt_State *L = spt_newstate();
  printf("== SPT core demo ==\n\n");

  /* ---------- 1. typed integer arithmetic: (2 + 3) * 4 ---------- */
  {
    const Instr code[] = {
      SPT_MK_AsBx(OP_LOADINT, 1, 2),
      SPT_MK_AsBx(OP_LOADINT, 2, 3),
      SPT_MK_ABC (OP_IADD,    1, 1, 2),
      SPT_MK_AsBx(OP_LOADINT, 2, 4),
      SPT_MK_ABC (OP_IMUL,    1, 1, 2),
      SPT_MK_ABC (OP_RETURN,  1, 2, 0),
    };
    Proto *p = make_proto(L, code, (int)(sizeof(code)/sizeof((code)[0])), 0, 8);
    run_chunk(L, p, 1);
    printf("1. typed int   (2+3)*4         = %lld\n", (long long)spt_toint(L, 1));
    reset(L);
  }

  /* ---------- 2. List: build [10,20,30], read [1], take length ---------- */
  {
    const Instr code[] = {
      SPT_MK_ABC (OP_NEWLIST,  1, 4, 0),
      SPT_MK_AsBx(OP_LOADINT,  2, 10), SPT_MK_ABC(OP_LISTPUSH, 1, 2, 0),
      SPT_MK_AsBx(OP_LOADINT,  2, 20), SPT_MK_ABC(OP_LISTPUSH, 1, 2, 0),
      SPT_MK_AsBx(OP_LOADINT,  2, 30), SPT_MK_ABC(OP_LISTPUSH, 1, 2, 0),
      SPT_MK_AsBx(OP_LOADINT,  2, 1),
      SPT_MK_ABC (OP_GETLIST,  3, 1, 2),   /* R3 = list[1] = 20 */
      SPT_MK_ABC (OP_LEN,      4, 1, 0),   /* R4 = #list  = 3  */
      SPT_MK_ABC (OP_RETURN,   3, 3, 0),   /* return R3, R4    */
    };
    Proto *p = make_proto(L, code, (int)(sizeof(code)/sizeof((code)[0])), 0, 8);
    run_chunk(L, p, 2);
    printf("2. list        [10,20,30][1]   = %lld   (length %lld, 0-based)\n",
           (long long)spt_toint(L, 1), (long long)spt_toint(L, 2));
    reset(L);
  }

  /* ---------- 3. Map: m["hp"] = 100; read it back ---------- */
  {
    const char *consts[] = { "hp" };
    const Instr code[] = {
      SPT_MK_ABC (OP_NEWMAP,  1, 0, 0),
      SPT_MK_ABx (OP_LOADK,   2, 0),       /* R2 = "hp"        */
      SPT_MK_AsBx(OP_LOADINT, 3, 100),
      SPT_MK_ABC (OP_SETMAP,  1, 2, 3),    /* m["hp"] = 100    */
      SPT_MK_ABx (OP_LOADK,   2, 0),
      SPT_MK_ABC (OP_GETMAP,  4, 1, 2),    /* R4 = m["hp"]     */
      SPT_MK_ABC (OP_RETURN,  4, 2, 0),
    };
    Proto *p = make_proto(L, code, (int)(sizeof(code)/sizeof((code)[0])), 0, 8);
    set_string_consts(L, p, consts, 1);
    run_chunk(L, p, 1);
    printf("3. map         m[\"hp\"]          = %lld\n", (long long)spt_toint(L, 1));
    reset(L);
  }

  /* ---------- 4. C interop + globals: call add(7,35), store as _G.answer ---- */
  {
    spt_register(L, "add", c_add);
    const char *consts[] = { "add", "answer" };
    const Instr code[] = {
      SPT_MK_ABx (OP_GETGLOBAL, 1, 0),     /* R1 = _G["add"]   */
      SPT_MK_ABC (OP_LOADNULL,  2, 0, 0),  /* R2 = receiver    */
      SPT_MK_AsBx(OP_LOADINT,   3, 7),
      SPT_MK_AsBx(OP_LOADINT,   4, 35),
      SPT_MK_ABC (OP_CALL,      1, 3, 2),  /* R1 = add(7,35)   */
      SPT_MK_ABx (OP_SETGLOBAL, 1, 1),     /* _G["answer"] = R1*/
      SPT_MK_ABC (OP_RETURN,    1, 2, 0),
    };
    Proto *p = make_proto(L, code, (int)(sizeof(code)/sizeof((code)[0])), 0, 8);
    set_string_consts(L, p, consts, 2);
    run_chunk(L, p, 1);
    printf("4. C interop   add(7, 35)      = %lld\n", (long long)spt_toint(L, 1));
    reset(L);
    spt_getglobal(L, "answer");
    printf("   global read _G.answer       = %lld\n", (long long)spt_toint(L, 1));
    reset(L);
  }

  /* ---------- 5. closures: an SPT function calling an SPT function ---------- */
  {
    /* nested: int addfn(int a, int b) { return a + b; }  (R0=recv,R1=a,R2=b) */
    const Instr inner[] = {
      SPT_MK_ABC(OP_IADD,   3, 1, 2),
      SPT_MK_ABC(OP_RETURN, 3, 2, 0),
    };
    Proto *addfn = make_proto(L, inner, (int)(sizeof(inner)/sizeof((inner)[0])), 2, 6);

    const Instr outer[] = {
      SPT_MK_ABx (OP_CLOSURE,  1, 0),      /* R1 = closure(addfn) */
      SPT_MK_ABC (OP_LOADNULL, 2, 0, 0),   /* receiver            */
      SPT_MK_AsBx(OP_LOADINT,  3, 100),
      SPT_MK_AsBx(OP_LOADINT,  4, 23),
      SPT_MK_ABC (OP_CALL,     1, 3, 2),   /* R1 = addfn(100,23)  */
      SPT_MK_ABC (OP_RETURN,   1, 2, 0),
    };
    Proto *p = make_proto(L, outer, (int)(sizeof(outer)/sizeof((outer)[0])), 0, 8);
    p->np = 1;
    p->p = (Proto **)spt_alloc(L, sizeof(Proto *));
    p->p[0] = addfn;
    run_chunk(L, p, 1);
    printf("5. closure     addfn(100, 23)  = %lld\n", (long long)spt_toint(L, 1));
    reset(L);
  }

  /* ---------- 6. GC: make 2000 dead strings, then reclaim them ---------- */
  {
    size_t before = spt_gc_count(L);
    for (int i = 0; i < 2000; i++) {
      char buf[32];
      snprintf(buf, sizeof buf, "garbage_%d", i);
      spt_pushstring(L, buf);
      spt_pop(L, 1);                  /* immediately unreachable */
    }
    size_t peak = spt_gc_count(L);
    spt_gc_collect(L);
    size_t after = spt_gc_count(L);
    printf("6. gc          live objects: %zu -> %zu (peak) -> %zu (after collect)\n",
           before, peak, after);
    reset(L);
  }

  /* ---------- 7. JIT (only in JIT builds) ---------- */
#ifdef SPT_HAS_JIT
  demo_jit(L);
#else
  printf("7. jit         (interpreter-only build — JIT disabled)\n");
#endif

  printf("\n== all demos completed ==\n");
  spt_close(L);
  return 0;
}

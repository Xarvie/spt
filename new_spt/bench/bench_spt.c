/*
 * bench/bench_spt.c — time SPT benchmark programs.
 *
 * Built twice from the same source:
 *   - interpreter mode: compiled WITHOUT -DSPT_JIT, so every prototype always
 *     runs through the interpreter (no auto-compile path exists).
 *   - JIT mode: compiled WITH -DSPT_JIT; each benchmark's `bench` prototype is
 *     force-compiled to native before timing.
 *
 * Each benchmark file defines `bench(int n)`; we call it best-of-K times after a
 * couple of warmup calls and report the minimum wall-clock time (the least-noisy
 * estimator). One line per benchmark: "<name>\t<seconds>\t<result>".
 */
#include "spt.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef SPT_HAS_JIT
extern void spt_jit_try_compile(spt_State *, Proto *);
#endif

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc((size_t)n + 1);
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "read err\n"); exit(2); }
  buf[n] = '\0'; fclose(f); return buf;
}

static double now_sec(void) {
  struct timespec ts; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);  /* match Lua os.clock() */
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* call bench(n); read the result as a double (works for int or float results) */
static double call_bench(spt_State *L, long long n) {
  L->top = L->stack;
  spt_getglobal(L, "bench"); setnull(L->top); L->top++;
  spt_pushint(L, n);
  spt_call(L, 1, 1);
  double r = spt_tofloat(L, 1);
  L->top = L->stack;
  return r;
}

typedef struct { const char *name, *file; long long n; } Bench;

int main(int argc, char **argv) {
  /* name, source file, problem size */
  Bench benches[] = {
    { "intloop",   "bench/intloop.spt",   20000000 },
    { "floatloop", "bench/floatloop.spt", 10000000 },
    { "listsum",   "bench/listsum.spt",        3000 },
    { "mapsum",    "bench/mapsum.spt",         2000 },
    { "fib",       "bench/fib.spt",              32 },
  };
  int nb = (int)(sizeof benches / sizeof benches[0]);
  const int WARM = 2, REPS = 7;

#ifdef SPT_HAS_JIT
  const char *mode = "jit";
#else
  const char *mode = "interp";
#endif
  (void)argc; (void)argv;

  for (int b = 0; b < nb; b++) {
    spt_State *L = spt_newstate();
    char *src = read_file(benches[b].file);
    if (spt_load(L, src, benches[b].name) != 0) {
      fprintf(stderr, "%s: compile error: %s\n", benches[b].name, L->errmsg);
      free(src); spt_close(L); continue;
    }
    setnull(L->top); L->top++; spt_call(L, 0, 0); L->top = L->stack;  /* bind globals */

#ifdef SPT_HAS_JIT
    spt_getglobal(L, "bench");
    Proto *p = clvalue(L->top - 1)->p;
    L->top = L->stack;
    spt_jit_try_compile(L, p);
    if (!p->jit_entry) {
      fprintf(stderr, "%s: WARNING bench did not compile (running interpreted)\n",
              benches[b].name);
    }
#endif

    long long n = benches[b].n;
    double result = 0;
    for (int w = 0; w < WARM; w++) result = call_bench(L, n);

    double best = 1e30;
    for (int r = 0; r < REPS; r++) {
      double t0 = now_sec();
      result = call_bench(L, n);
      double dt = now_sec() - t0;
      if (dt < best) best = dt;
    }

    printf("%s\t%.6f\t%.6g\n", benches[b].name, best, result);
    fflush(stdout);
    free(src);
    spt_close(L);
  }
  (void)mode;
  return 0;
}

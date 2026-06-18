# SPT benchmarks

Three-way comparison of the **SPT interpreter**, the **SPT MIR-JIT**, and the
reference **PUC-Lua 5.4** interpreter on a set of small CPU-bound kernels.

The goal is not to win a shoot-out but to get *actionable* numbers: where the JIT
pays off, where it doesn't, and how the from-scratch interpreter stacks up against
a mature reference implementation.

## How to run

```
bench/run.sh
```

It builds two SPT drivers from `bench/bench_spt.c` — one **without** the JIT
(`-DSPT_JIT` omitted, so every prototype runs interpreted) and one **with** the
JIT (each benchmark's `bench` prototype is force-compiled before timing) — then
runs the matching Lua programs through `lua5.4`, and prints the table below.

## Methodology

- Each benchmark is a single function `bench(int n)` doing a fixed, self-contained
  amount of work. The same algorithm is written twice — once in SPT
  (`bench/<name>.spt`) and once in Lua (`bench/<name>.lua`) — and the two are kept
  numerically identical (no reliance on overflow/precision differences).
- **All three engines produce bit-identical results** on every benchmark; the
  runner aborts a row if they ever diverge. This makes the timings comparable.
- Timing is **best-of-7 process CPU seconds** after 2 warmup calls. SPT uses
  `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`; Lua uses `os.clock()` — both are
  process CPU time. The minimum is the least-noisy estimator.
- Only execution of the kernel is timed; parsing and (for the JIT) compilation
  happen before the timed region.

## Results

Representative best-of-7 run (lower seconds = faster; ratios are derived):

| benchmark  | interp (s) | jit (s) | lua (s) | jit/intp | lua/jit | lua/intp |
|------------|-----------:|--------:|--------:|---------:|--------:|---------:|
| intloop    |      1.07  |  0.163  |  0.360  |   6.6x   |  2.21x  |  0.34x   |
| floatloop  |      0.53  |  0.089  |  0.163  |   6.0x   |  1.82x  |  0.31x   |
| listsum    |      0.39  |  0.099  |  0.093  |   4.0x   |  0.94x  |  0.24x   |
| mapsum     |      0.175 |  0.066  |  0.041  |   2.6x   |  0.62x  |  0.23x   |
| fib        |      0.47  |  0.233  |  0.133  |   2.0x   |  0.57x  |  0.28x   |

- **jit/intp** — SPT-JIT speedup over the SPT interpreter (higher is better).
- **lua/jit** — `> 1` means SPT-JIT is faster than PUC-Lua; `< 1` means slower.
- **lua/intp** — `> 1` means the SPT interpreter is faster than PUC-Lua; `< 1` slower.

(Absolute numbers vary a few percent run to run and depend on the machine; the
ratios are stable.)

### The kernels

- **intloop** — tight typed-integer arithmetic (`IMUL/IADD/ISUB/IMOD` + branch).
- **floatloop** — typed-float arithmetic (`FADD/FMUL/FSUB`).
- **listsum** — build a list, then sum it repeatedly; the inner loop is the JIT's
  inline bounds-checked array load (`OP_GETLIST`).
- **mapsum** — insert `n` integer-keyed entries into a map, then sum all values
  `n` times; exercises the hash-table fast paths (`OP_SETMAP/OP_GETMAP`).
- **fib** — naive recursive Fibonacci; call-bound (`OP_CALL`).

## What the numbers say

1. **The JIT delivers a 2–6.6× speedup over SPT's own interpreter.** The win is
   largest on tight arithmetic (≈6.6× int, ≈6× float), where the lowered code is
   essentially native integer/float ops with no dispatch, and smaller on
   call-, container-, and hash-bound code.

2. **Against PUC-Lua, the JIT wins on arithmetic, ties on list access, and loses
   on hashing and recursion.** SPT-JIT is ≈2.2× faster than reference Lua on
   integer loops and ≈1.8× on float loops — a real result for straight-line typed
   arithmetic. It roughly ties on `listsum`. It loses to Lua on `mapsum` (≈1.6×)
   and `fib` (≈1.75×).

3. **The two weak spots are concrete and actionable:**
   - **The call path.** `fib` is almost pure call overhead, and SPT pays
     `do_call` + `spt_jit_enter` + full CallInfo/frame setup on every call. This
     is the JIT's single biggest optimization target.
   - **The hash-map path.** SPT's separate List/Map design uses a simpler hash
     than Lua's heavily-tuned unified table, so `mapsum` trails.

4. **The SPT interpreter is ≈3–4× slower than PUC-Lua's interpreter across the
   board.** PUC-Lua 5.4's interpreter is exceptionally well tuned; SPT's typed
   opcodes and List/Map split do not, as measured here, make the *interpreter*
   faster than stock Lua. The JIT is what closes (and on arithmetic, reverses)
   that gap. Earlier documentation claiming the interpreter beats stock Lua is not
   supported by these measurements.

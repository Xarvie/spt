#!/usr/bin/env python3
"""Differential JIT fuzzer for SPT.

Generates many valid, deterministic SPT programs that exercise JIT-able patterns
(scalar arithmetic, array read/write, repeated-element CSE, two-array, chained
2D, branches, floats) with randomized shapes, then runs each with JIT off vs on
and reports any output mismatch. A mismatch is a real correctness bug.

Generated programs are kept safe: indices stay in bounds, no division/mod by
zero (arithmetic uses + - * only), each program is type-consistent and
deterministic. Reproducible via --seed.

Usage: python3 scripts/jit_fuzz.py [--seed N] [--count K] [--bin PATH] [--hot H]
"""
import argparse, os, random, subprocess, sys, tempfile

AOPS = ["+", "-", "*"]
COPS = ["<", "<=", ">", ">=", "==", "!="]

def rconst(r, lo=1, hi=9):
    return r.randint(lo, hi)

def gen_scalar(r):
    n = r.choice([50000, 200000, 1000000])
    init = r.randint(0, 5)
    o1, o2 = r.choice(AOPS), r.choice(AOPS)
    k1, k2 = rconst(r), rconst(r)
    m = r.choice([1000, 100000, 1000000007])
    return (f"int s = {init};\n"
            f"for (int i = 1, {n}) {{ s = (s {o1} (i {o2} {k1})) % {m}; "
            f"s = s {r.choice(AOPS)} {k2}; }}\nprint(s);\n")

def gen_array_reduce(r, typ="int"):
    sz = r.randint(1, 12)
    if typ == "int":
        vals = ", ".join(str(r.randint(-9, 20)) for _ in range(sz))
        decl, acc, op = f"list<int> a = [{vals}];", "int s = 0;", r.choice(AOPS)
    else:
        vals = ", ".join(f"{r.randint(1,40)}.{r.choice([0,25,5,75,125])}" for _ in range(sz))
        decl, acc, op = f"list<float> a = [{vals}];", "float s = 0.0;", r.choice(["+", "-"])
    rn = r.choice([20000, 50000])
    return (f"{decl}\n{acc}\n"
            f"for (int r = 0, {rn}) {{ for (int j = 0, {sz-1}) {{ s = s {op} a[j]; }} }}\n"
            f"print(s);\n")

def gen_cse_self(r):
    sz = r.randint(1, 10)
    vals = ", ".join(str(r.randint(1, 12)) for _ in range(sz))
    op = r.choice(AOPS)
    extra = r.choice(["", " + a[j]", " - a[j]"])
    return (f"list<int> a = [{vals}];\nint s = 0;\n"
            f"for (int r = 0, 50000) {{ for (int j = 0, {sz-1}) {{ "
            f"s = s + a[j] {op} a[j]{extra}; }} }}\nprint(s);\n")

def gen_two_array(r):
    sz = r.randint(1, 10)
    av = ", ".join(str(r.randint(-5, 15)) for _ in range(sz))
    bv = ", ".join(str(r.randint(-5, 15)) for _ in range(sz))
    op = r.choice(AOPS)
    return (f"list<int> a = [{av}];\nlist<int> b = [{bv}];\nint s = 0;\n"
            f"for (int r = 0, 50000) {{ for (int j = 0, {sz-1}) {{ "
            f"s = s + a[j] {op} b[j]; }} }}\nprint(s);\n")

def gen_write_read(r):
    sz = r.randint(1, 10)
    vals = ", ".join(str(r.randint(0, 10)) for _ in range(sz))
    k = rconst(r)
    op = r.choice(["+", "-", "*"])
    return (f"list<int> a = [{vals}];\nint s = 0;\n"
            f"for (int r = 0, 30000) {{ for (int j = 0, {sz-1}) {{ "
            f"a[j] = a[j] {op} {k}; s = s + a[j]; }} }}\nprint(s);\n")

def gen_chained2d(r):
    rows = r.randint(1, 4)
    cols = r.randint(1, 6)
    mat = ",".join("[" + ",".join(str(r.randint(-3, 12)) for _ in range(cols)) + "]"
                   for _ in range(rows))
    rn = r.choice([15000, 40000])
    # variable outer, or a fixed valid outer index
    if r.random() < 0.5 and rows >= 1:
        ci = r.randint(0, rows - 1)
        body = (f"for (int j = 0, {cols-1}) {{ s = s {r.choice(AOPS)} m[{ci}][j]; }}")
    else:
        body = (f"for (int i = 0, {rows-1}) {{ for (int j = 0, {cols-1}) {{ "
                f"s = s {r.choice(AOPS)} m[i][j]; }} }}")
    return (f"list<list<int>> m = [{mat}];\nint s = 0;\n"
            f"for (int r = 0, {rn}) {{ {body} }}\nprint(s);\n")

def gen_branch(r):
    n = r.choice([200000, 1000000])
    cop = r.choice(COPS)
    k = r.randint(0, 50)
    a, b = r.randint(1, 9), r.randint(1, 9)
    md = r.choice([2, 3, 4, 7])
    cond = r.choice([f"i {cop} {k}", f"i % {md} == 0", f"(i % {md}) {cop} {r.randint(0, md)}"])
    return (f"int s = 0;\n"
            f"for (int i = 0, {n}) {{ if ({cond}) {{ s = s + {a}; }} "
            f"else {{ s = s + {b}; }} }}\nprint(s);\n")

def gen_side_store(r):
    # Phase-2 side-trace shapes: a branch with a side-effecting (array-store) arm.
    # If-conversion bails on the store, so the root trace takes a guarded branch
    # and the minority arm becomes a hot side exit that gets compiled as a side
    # trace. Sweeps: which arm stores, branch bias (50/50 vs rare vs common), and
    # whether the OTHER arm also has a store (both arms non-if-convertible). The
    # side trace must record the direction the parent did NOT, and linked
    # execution must match the interpreter incl. all array contents.
    sz = 8
    av = ", ".join("0" for _ in range(sz))
    n = r.choice([200000, 300000, 1000000])
    md = r.choice([2, 2, 3, 5, 10])           # 2 => 50/50; larger => biased
    rel = r.choice(["==", "!="])               # which side is the store arm
    store_then = r.choice([True, False])       # store in then- vs else-arm
    both = r.choice([True, False])             # both arms store (2 arrays)
    cond = f"i % {md} {rel} 0"
    store_a = "a[k % 8] = i; k = k + 1;"
    accum = "s = s + i;"
    if both:
        store_b = "b[m % 8] = i + 1; m = m + 1;"
        then_body = store_a if store_then else store_b
        else_body = store_b if store_then else store_a
        decl = (f"list<int> a = [{av}];\nlist<int> b = [{av}];\n"
                f"int s = 0;\nint k = 0;\nint m = 0;\n")
        tail = ("print(k);\nprint(m);\n"
                "for (int j = 0, 7) { print(a[j]); }\n"
                "for (int j = 0, 7) { print(b[j]); }\n")
    else:
        then_body = store_a if store_then else accum
        else_body = accum if store_then else store_a
        decl = f"list<int> a = [{av}];\nint s = 0;\nint k = 0;\n"
        tail = ("print(s);\nprint(k);\n"
                "for (int j = 0, 7) { print(a[j]); }\n")
    return (decl +
            f"for (int i = 1, {n}) {{ if ({cond}) {{ {then_body} }} "
            f"else {{ {else_body} }} }}\n" + tail)


def gen_unroll_nest(r):
    """Nested loop with a SHORT, CONSTANT-bound inner loop -- the case
    inner-loop unrolling (Phase 3a) targets. Sweeps inner trip count across the
    UNROLL_MAX boundary (1..18), step (incl. negative), non-zero init, and inner
    body kind (scalar / float / array read / array write). The fuzzer also
    sweeps SPT_JIT_UNROLL_MAX, so both the unrolled and the fall-back (skip+abort
    the outer trace) paths are exercised against the interpreter. A wrong trip
    count or stale loop-index in the unroll shows up immediately as a sum
    mismatch. Deterministic."""
    outer = r.choice([20000, 100000, 300000])
    step = r.choice([1, 1, 1, 2, 3, -1, -2])
    trips = r.randint(1, 18)
    if step > 0:
        init = r.choice([0, 0, 1, 3])
        limit = init + (trips - 1) * step
    else:
        init = r.choice([8, 10, 14, 17])
        limit = init + (trips - 1) * step
        if limit < 0:
            limit = 0                      # keep indices valid; trip count just shrinks
    maxidx = max(init, limit)              # j stays within [min,max]; both >= 0
    hdr = (f"for (int j = {init}, {limit})" if step == 1
           else f"for (int j = {init}, {limit}, {step})")
    kind = r.choice(["scalar", "float", "arr_read", "arr_write"])
    if kind == "scalar":
        op = r.choice(AOPS)
        return (f"int s = 0;\n"
                f"for (int t = 0, {outer}) {{ {hdr} {{ s = s + (j {op} 2); }} }}\n"
                f"print(s);\n")
    if kind == "float":
        return (f"float s = 0.0;\n"
                f"for (int t = 0, {outer}) {{ {hdr} {{ s = s + j * 1.5; }} }}\n"
                f"print(s);\n")
    vals = ", ".join(str((q * 3 + 1) % 7) for q in range(maxidx + 1))
    if kind == "arr_read":
        return (f"list<int> a = [{vals}];\n"
                f"int s = 0;\n"
                f"for (int t = 0, {outer}) {{ {hdr} {{ s = s + a[j]; }} }}\n"
                f"print(s);\n")
    return (f"list<int> a = [{vals}];\n"
            f"for (int t = 0, {min(outer, 80000)}) {{ {hdr} {{ a[j] = a[j] + 1; }} }}\n"
            f"print(a[0] + a[{maxidx}]);\n")


def gen_moddiv(r):
    """Exercise %% and ~/ with potentially-negative dividends and both signs of
    divisor (floored-semantics path). Divisors are nonzero constants."""
    n = r.choice([200000, 1000000])
    d1 = r.choice([2, 3, 4, 5, 7, 1000, -3, -7])   # nonzero
    d2 = r.choice([2, 3, 5, -4])
    o = r.choice(AOPS)
    # dividend (s o (i o k)) can go negative; choose op/init to make it so
    init = r.randint(-5, 5)
    use = r.choice(["mod", "idiv", "both"])
    if use == "mod":
        body = f"s = (s {o} i) % {d1};"
    elif use == "idiv":
        body = f"s = s + ((init0 - i) ~/ {d1});".replace("init0", str(r.randint(0, 9)))
    else:
        body = f"int x = (0 - i) % {d1}; int y = (0 - i) ~/ {d2}; s = s + x + y;"
    return (f"int s = {init};\nfor (int i = 1, {n}) {{ {body} }}\nprint(s);\n")

def gen_float_moddiv(r):
    """Float %% / ~/ -- must abort and fall back to the (correct) interpreter."""
    a = f"{r.randint(1,40)}.{r.choice([0,25,5,75])}"
    b = f"{r.randint(1,9)}.{r.choice([0,5,25])}"
    op = r.choice(["%", "~/"])
    return (f"float a = {a};\nfloat b = {b};\nfloat s = 0.0;\n"
            f"for (int i = 0, 500000) {{ s = a {op} b; }}\nprint(s);\n")

def gen_multi_accum(r):
    """Several loop-carried accumulators updated together (register pressure)."""
    n = r.choice([200000, 1000000])
    m = r.choice([1000, 1000000007])
    return (f"int s1 = 0;\nint s2 = 1;\nint s3 = 0;\nint s4 = 0;\n"
            f"for (int i = 1, {n}) {{ s1 = s1 + i; s2 = (s2 * 3 + i) % {m}; "
            f"s3 = s3 ^ i; s4 = s4 + (i % 5); }}\n"
            f"print(s1); print(s2); print(s3); print(s4);\n")

def gen_recursive_nest(r):
    """All-constant nested loops collapsed by RECURSIVE inner-loop unrolling
    (Phase 3a extension): an outer rep loop wraps 2-3 constant-bound inner loops
    so every inner level unrolls inline into the rep trace (a fixed-size matrix
    multiply is the motivating case). Covers the bug class where a loop variable
    is an operand of an MMBIN that follows arithmetic (e.g. `i*k` makes the
    control slot an MMBIN operand -- must NOT be mistaken for a control-slot
    write), plus nested array reads/writes with indices that become constants
    after unrolling. The fuzzer sweeps SPT_JIT_UNROLL_MAX, so partial-unroll and
    full-fall-back paths are also hit. Deterministic."""
    rep = r.choice([50000, 200000])
    depth = r.choice([2, 2, 3])
    # small constant trips so the nest fully unrolls (product stays bounded)
    trips = [r.randint(2, 4) for _ in range(depth)]
    vs = ["i", "j", "k"][:depth]
    hdrs = "".join(f"for (int {vs[d]} = 0, {trips[d]-1}) {{ " for d in range(depth))
    close = "}" * depth
    kind = r.choice(["scalar_mmbin", "arr_read", "arr_write"])
    if kind == "scalar_mmbin":
        # loop vars as MMBIN operands: i*i - i, (i*j)+k, etc.
        o1 = r.choice(AOPS); o2 = r.choice(AOPS)
        expr = f"({vs[0]} {o1} {vs[0]}) {o2} {vs[-1]}"
        return (f"int s = 0;\n"
                f"for (int rep = 0, {rep}) {{ {hdrs} s = s + {expr}; {close} }}\n"
                f"print(s);\n")
    # array cases: index = a constant combination of the loop vars (max < n)
    maxv = sum(t - 1 for t in trips)            # max index reached by sum of vars
    n = maxv + 1
    vals = ", ".join(str((q * 2 + 1) % 9) for q in range(n))
    idx = " + ".join(vs)
    if kind == "arr_read":
        return (f"list<int> a = [{vals}];\n"
                f"int s = 0;\n"
                f"for (int rep = 0, {rep}) {{ {hdrs} s = s + a[{idx}]; {close} }}\n"
                f"print(s);\n")
    return (f"list<int> a = [{vals}];\n"
            f"for (int rep = 0, {min(rep,60000)}) {{ {hdrs} a[{idx}] = a[{idx}] + 1; {close} }}\n"
            f"print(a[0] + a[{maxv}]);\n")


def gen_speculative_nest(r):
    """Nested loops whose INNER bound is a loop-INVARIANT VARIABLE (not a
    compile-time constant): `for j = 0, N-1`, `for j = 0, N`, variable init
    `for j = M, N`, variable step `for j = 0, N, S`, and a two-level case whose
    outer constant index pins the inner variable bound. These exercise the
    GUARDED SPECULATIVE unroller (Phase 3a): the recorder reads the invariant
    bound's value, pins it with a run-time guard (two GUARD_LE), and unrolls the
    speculated trip count -- a variable-dimension matrix multiply / gemv is the
    motivating case. Correctness rests entirely on the guard (a wrong or stale
    guess just side-exits), so output must match the interpreter under every
    SPT_JIT_UNROLL_MAX. Bodies never read AND write the same array (that path
    bails). Deterministic."""
    rep = r.choice([50000, 200000])
    N = r.randint(3, 8)                 # invariant dimension, set once
    M = r.randint(0, 2)
    S = r.choice([1, 2, 3])
    use_arr = r.random() < 0.5
    style = r.choice(["limit_minus", "limit_direct", "init_var", "step_var", "two_level"])
    setup = f"int N = {N};\n"
    if use_arr:
        vals = ", ".join(str((q * 3 + 1) % 7) for q in range(N + 1))
        setup += f"list<int> a = [{vals}];\n"
        rd = "a[j]"
    else:
        rd = "j"
    if style == "limit_minus":
        inner = f"for (int j = 0, N-1) {{ s = s + {rd}; }}"
    elif style == "limit_direct":
        inner = f"for (int j = 0, N) {{ s = s + {rd}; }}"
    elif style == "init_var":
        setup += f"int M = {M};\n"
        inner = f"for (int j = M, N) {{ s = s + {rd}; }}"
    elif style == "step_var":
        setup += f"int S = {S};\n"
        inner = f"for (int j = 0, N, S) {{ s = s + {rd}; }}"
    else:  # two_level: outer constant i is pinned -> inner N stays speculative
        rd2 = "a[j]" if use_arr else "(i + j)"
        inner = f"for (int i = 0, 3) {{ for (int j = 0, N-1) {{ s = s + {rd2}; }} }}"
    return (setup +
            f"int s = 0;\n"
            f"for (int rep = 0, {rep}) {{ {inner} }}\n"
            f"print(s);\n")


def gen_map_access(r):
    """Map reads by constant short-string keys in a hot loop: exercises the
    inline hash-slot GETFIELD fast path. The recorder records a read only when
    the key sits at its MAIN POSITION in the live map (else it aborts and the
    interpreter handles it); since the string hash is seeded per run, a given
    key may or may not be at its main position, so this sweeps BOTH the
    fast-path-compiled and the collision-fallback cases. Either way the value is
    the same as the interpreter's (correctness rests on the key+type guards plus
    side-exit), so output is deterministic and the differential gate is stable.
    Covers int- and float-valued maps, several keys, and a map value used as a
    list index (the m[k] result feeding a guarded GETI). Deterministic."""
    KEYS = ["a", "b", "c", "x", "y", "z", "key", "val", "name", "id",
            "alpha", "beta", "gamma", "lo", "hi", "sum", "n", "k", "q", "w"]
    rep = r.choice([50000, 200000])
    nkeys = r.randint(2, 6)
    keys = r.sample(KEYS, nkeys)
    flt = r.random() < 0.4
    if flt:
        pairs = ", ".join(f'"{k}": {r.randint(1, 9)}.{r.randint(0, 9)}' for k in keys)
        decl = f"map<str, float> m = {{{pairs}}};"
        acc0 = "0.0"
    else:
        pairs = ", ".join(f'"{k}": {r.randint(1, 40)}' for k in keys)
        decl = f"map<str, int> m = {{{pairs}}};"
        acc0 = "0"
    # read 1..3 keys, combined with +/- (and * for a non-zero start)
    nread = r.randint(1, min(3, nkeys))
    rk = r.sample(keys, nread)
    ops = ["+", "-", "+"]
    expr = f'm["{rk[0]}"]'
    for j in range(1, nread):
        expr += f' {ops[j % 3]} m["{rk[j]}"]'
    style = r.choice(["accum", "accum", "idx"])
    if style == "idx" and not flt:
        # use an int map value (0..n-1) as a list index
        n = 8
        vals = ", ".join(str((q * 2 + 1) % 13) for q in range(n))
        ik = keys[0]
        # clamp the indexing key's value into range via a fresh small map
        decl = (f"map<str, int> m = {{" +
                ", ".join(f'"{k}": {r.randint(0, n - 1)}' for k in keys) + "};\n"
                f"list<int> arr = [{vals}];")
        return (decl + "\n"
                f"int s = 0;\n"
                f"for (int i = 0, {rep}) {{ s = s + arr[m[\"{ik}\"]]; }}\n"
                f"print(s);\n")
    return (decl + "\n"
            f"{'float' if flt else 'int'} s = {acc0};\n"
            f"for (int i = 0, {rep}) {{ s = s {('+' if flt else '+')} {expr}; }}\n"
            f"print(s);\n")


def gen_map_write(r):
    """Map WRITES by constant short-string keys in a hot loop: exercises the
    inline hash-slot SETFIELD fast path (and GETFIELD for the read side of an
    accumulate). Only int/float values are written (no GC barrier), and only an
    EXISTING main-position key updates on the fast path -- an absent/chained key
    or a resized table side-exits and the interpreter stores. The classic shape
    is `m[k] = m[k] + x` (read-modify-write the same key, like a counter or a
    histogram bucket). As with gen_map_access the hash is seeded per run, so
    whether the write JITs varies, but the resulting map is identical to the
    interpreter's -- output is deterministic, gate stable. Deterministic."""
    KEYS = ["count", "sum", "total", "acc", "n", "hi", "lo", "x", "y", "k", "s", "v"]
    rep = r.choice([50000, 200000])
    nkeys = r.randint(1, 4)
    keys = r.sample(KEYS, nkeys)
    flt = r.random() < 0.35
    if flt:
        decl = "map<str, float> m = {" + ", ".join(f'"{k}": 0.0' for k in keys) + "};"
        inc = f'{r.randint(1, 5)}.{r.randint(0, 9)}'
    else:
        decl = "map<str, int> m = {" + ", ".join(f'"{k}": 0' for k in keys) + "};"
        inc = str(r.randint(1, 7))
    wk = keys[0]
    style = r.choice(["rmw_const", "rmw_self", "two_keys", "nested_arr"])
    if style == "rmw_const":
        body = f'm["{wk}"] = m["{wk}"] + {inc};'
        loop = f"for (int i = 0, {rep}) {{ {body} }}"
    elif style == "two_keys" and nkeys >= 2:
        k2 = keys[1]
        body = f'm["{wk}"] = m["{wk}"] + {inc}; m["{k2}"] = m["{k2}"] - {inc};'
        loop = f"for (int i = 0, {rep}) {{ {body} }}"
    elif style == "nested_arr" and not flt:
        n = 8
        vals = ", ".join(str((q * 2 + 1) % 11) for q in range(n))
        decl = decl + f"\nlist<int> a = [{vals}];"
        loop = (f"for (int rep = 0, {min(rep, 80000)}) {{ for (int j = 0, 7) {{ "
                f'm["{wk}"] = m["{wk}"] + a[j]; }} }}')
    else:  # rmw_self: accumulate using the running value
        body = f'm["{wk}"] = m["{wk}"] + {inc};'
        loop = f"for (int i = 0, {rep}) {{ {body} }}"
    rd = f'print(m["{wk}"]);'
    return decl + "\n" + loop + "\n" + rd + "\n"


def gen_nested3(r):
    a = r.randint(2, 50); b = r.randint(2, 12); c = r.randint(2, 8)
    o1 = r.choice(AOPS); o2 = r.choice(AOPS)
    return (f"int s = 0;\n"
            f"for (int x = 0, {a}) {{ for (int y = 0, {b}) {{ for (int z = 0, {c}) {{ "
            f"s = s + (x {o1} y) {o2} z; }} }} }}\nprint(s);\n")

def gen_bitwise_neg(r):
    n = r.choice([200000, 1000000])
    k1 = r.choice([255, 127, 65535, 15])
    s1, s2 = r.randint(1, 5), r.randint(1, 5)
    return (f"int s = 0;\n"
            f"for (int i = 1, {n}) {{ s = s + ((0 - i) & {k1}) "
            f"+ (~i & {r.choice([255,127])}) - ((i << {s1}) ^ (i >> {s2})); }}\n"
            f"print(s);\n")

def gen_mixed_cmp(r):
    n = r.choice([200000, 1000000])
    thr = f"{r.randint(50,500)}.0"
    k = r.randint(0, 100)
    cop = r.choice(COPS)
    return (f"float x = 0.0;\nint s = 0;\n"
            f"for (int i = 0, {n}) {{ x = x + 0.5; "
            f"if (x > {thr}) {{ x = 0.0; s = s + 1; }} "
            f"if (i {cop} {k}) {{ s = s + 2; }} }}\nprint(s);\n")

def gen_inline_call(r):
    """A pure straight-line leaf function called in a hot loop -- exercises call
    inlining. Body is arithmetic on the params (no division/mod by a param), so
    it is inlinable; the loop should JIT with the call inlined."""
    np = r.randint(1, 3)
    params = [f"p{k}" for k in range(np)]
    psig = ", ".join(f"int {p}" for p in params)
    terms = [f"{p} {r.choice(AOPS)} {r.randint(1,5)}" for p in params]
    body = " + ".join(terms)
    if r.random() < 0.6:
        body += f" + {r.choice(params)} * {r.choice(params)}"
    args = [r.choice([f"i", f"i % {r.randint(2,100)}", f"i + {r.randint(0,50)}",
                      f"(0 - i)"]) for _ in range(np)]
    call = "f(" + ", ".join(args) + ")"
    n = r.choice([200000, 1000000])
    twice = r.random() < 0.3
    rhs = f"{call} - {call}" if twice else call
    return (f"function f({psig}) {{ return {body}; }}\n"
            f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + {rhs}; }}\nprint(s);\n")

def gen_swap(r):
    """Loop-carried value permutations (a=b; b=t etc.). These must NOT use the
    no-back-edge-move residency path -- they exercise the RA permutation bail.
    All arithmetic wraps deterministically, so interp and JIT must agree."""
    n = r.choice([200000, 1000000])
    kind = r.randint(0, 2)
    if kind == 0:  # fibonacci-style
        return (f"int a = {r.randint(0,3)};\nint b = {r.randint(1,4)};\n"
                f"for (int i = 1, {n}) {{ int t = a + b; a = b; b = t; }}\n"
                f"print(a + b);\n")
    if kind == 1:  # 2-way swap with an update
        return (f"int x = {r.randint(1,9)};\nint y = {r.randint(1,9)};\n"
                f"for (int i = 0, {n}) {{ int t = x; x = y + i; y = t; }}\n"
                f"print(x + y * 2);\n")
    return (f"int a = {r.randint(1,5)};\nint b = {r.randint(1,5)};\nint c = {r.randint(1,5)};\n"
            f"for (int i = 0, {n}) {{ int t = a; a = b; b = c; c = t + i; }}\n"
            f"print(a + b * 3 + c * 5);\n")

def gen_copy_carry(r):
    """Loop-carried copies where one slot copies another that is then mutated in
    place (b=a; a=a+1). Exercises the stale-alias RA bail. Deterministic wrap."""
    n = r.choice([200000, 1000000])
    kind = r.randint(0, 2)
    if kind == 0:
        return (f"int a = {r.randint(0,5)};\nint b = {r.randint(0,5)};\n"
                f"for (int i = 0, {n}) {{ b = a; a = a + {r.randint(1,4)}; }}\n"
                f"print(a * 1000 + b);\n")
    if kind == 1:
        return (f"int a = {r.randint(1,5)};\nint b = 0;\nint c = 0;\n"
                f"for (int i = 0, {n}) {{ c = b; b = a; a = a + i; }}\n"
                f"print(a + b * 100 + c * 10000);\n")
    return (f"int a = {r.randint(1,9)};\nint b = {r.randint(1,9)};\n"
            f"for (int i = 0, {n}) {{ b = a * {r.randint(1,3)}; a = a + b; }}\n"
            f"print(a + b * 7);\n")

def gen_for_while(r):
    """Outer `for` containing an inner `while` -- the nested-loop case that must
    not hang and whose inner while should still JIT (OP_JMP back-edge must target
    start_pc to close, else abort; retry phase shift lets the inner while compile
    even when the threshold aligns with its trip count). Variants: pure vars,
    array read, array write, read+write+temp. All outputs deterministic."""
    outer = r.choice([100000, 400000])
    hi0 = r.choice([8, 9, 15, 20])
    kind = r.randint(0, 3)
    if kind == 0:
        return (f"int total = 0;\n"
                f"for (int i = 0, {outer}) {{\n"
                f"  int lo = 0; int hi = {hi0};\n"
                f"  while (lo < hi) {{ lo = lo + 1; hi = hi - 1; }}\n"
                f"  total = total + lo;\n}}\nprint(total);\n")
    sz = hi0 + 1
    arr = "[" + ",".join(str(r.randint(0, 9)) for _ in range(sz)) + "]"
    if kind == 1:
        return (f"list<int> v = {arr};\nint total = 0;\n"
                f"for (int i = 0, {outer}) {{\n  int lo = 0; int hi = {hi0};\n"
                f"  while (lo < hi) {{ total = total + v[lo]; lo = lo + 1; hi = hi - 1; }}\n"
                f"}}\nprint(total);\n")
    if kind == 2:
        return (f"list<int> v = {arr};\n"
                f"for (int i = 0, {outer}) {{\n  int lo = 0; int hi = {hi0};\n"
                f"  while (lo < hi) {{ v[lo] = lo + i; lo = lo + 1; hi = hi - 1; }}\n"
                f"}}\nprint(v[0] + v[1]);\n")
    return (f"list<int> v = {arr};\nint total = 0;\n"
            f"for (int i = 0, {outer}) {{\n  int lo = 0; int hi = {hi0};\n"
            f"  while (lo < hi) {{ int x = v[lo]; v[hi] = x; total = total + x; lo = lo + 1; hi = hi - 1; }}\n"
            f"}}\nprint(total);\n")

def gen_const_fold(r):
    """Ops on a loop-invariant integer constant assigned INSIDE the loop, which
    the recorder sees as KINT and constant-folds. Exercises the folder's logical
    >> / << (zero-fill + >=64 saturation + negative-count direction) and floored
    ~/ and % (sign-of-divisor), where C's signed operators diverge from SPT.
    Deterministic."""
    n = r.choice([200000, 1000000])
    a = r.choice([-100, -17, -8, -1, 0, 7, 13, 100, 1000, -9223372036854775807])
    op = r.choice([">>", "<<", "~/", "%"])
    if op in (">>", "<<"):
        b = r.choice([0, 1, 2, 3, 4, 31, 63, 64, 70, -2, -4])
    else:
        b = r.choice([-5, -4, -2, -1, 2, 3, 5, 7])
    return (f"int s = 0;\n"
            f"for (int i = 0, {n}) {{ int x = {a}; s = s + (x {op} {b}); }}\n"
            f"print(s);\n")

def gen_float_bitwise(r):
    """Bitwise &|^<<>> with at least one FLOAT operand. SPT converts floats to
    int (erroring on a fractional part); the JIT must ABORT these rather than
    operate on the raw IEEE bits. Uses integer-valued floats so the interpreter
    succeeds and the JIT (falling back to it) yields a comparable result.
    Deterministic."""
    n = r.choice([200000, 1000000])
    op = r.choice(["&", "|", "^", "<<", ">>", "~"])
    fv = r.choice([2.0, 5.0, 6.0, 12.0, 255.0])
    iv = r.choice([1, 2, 3, 10, 255])
    if op == "~":  # unary BNOT on a float operand -> must abort to interpreter
        return (f"int s = 0;\nfor (int i = 0, {n}) {{ float f = {fv}; "
                f"int x = ~f; s = s + x; }}\nprint(s);\n")
    if op in ("<<", ">>"):
        sh = r.choice([1.0, 2.0, 3.0, 4.0])
        if r.random() < 0.5:  # float value
            return (f"int s = 0;\nfor (int i = 0, {n}) {{ float v = {fv}; "
                    f"int x = v {op} {iv % 8}; s = s + x; }}\nprint(s);\n")
        return (f"int s = 0;\nfor (int i = 0, {n}) {{ float sh = {sh}; "
                f"int x = {iv} {op} sh; s = s + x; }}\nprint(s);\n")
    return (f"int s = 0;\nfor (int i = 0, {n}) {{ float b = {fv}; "
            f"int x = b {op} {iv}; s = s + x; }}\nprint(s);\n")

def gen_type_transition(r):
    """A loop-carried accumulator that changes int<->float mid-loop via a rare
    branch. The trace records and pins one type; once the value permanently
    changes type, the entry-time live-in guard must DECLINE (interpreter
    continues, advancing the loop) rather than livelock on a guard that never
    passes. Verifies no hang + byte-identical output. Deterministic."""
    n = r.choice([100000, 800000])
    sw = n // 2
    forms = [
        f"auto acc = 0;\nfor (int i = 0, {n}) {{ if (i == {sw}) {{ acc = acc + 0.5; }} acc = acc + 1; }}\nprint(acc);\n",
        f"auto acc = 1;\nfor (int i = 0, {n}) {{ if (i == {sw}) {{ acc = acc * 1.5; }} acc = acc + 2; }}\nprint(acc);\n",
        f"auto a = 0;\nfor (int i = 0, {n}) {{ if (i % 7 == 0) {{ a = a + 0.25; }} a = a + 1; }}\nprint(a);\n",
        f"auto x = 0;\nint s = 0;\nfor (int i = 0, {n}) {{ if (i == {sw}) {{ x = x + 0.5; }} s = s + 1; }}\nprint(s);\n",
        f"float s = 0.0;\nfor (int i = 0, {n}) {{ auto x = i; if (i % 2 == 0) {{ x = i * 1.5; }} s = s + x; }}\nprint(s);\n",
    ]
    return r.choice(forms)

def gen_string(r):
    """String ops in a hot loop: length (#s), compare (==), concat (..). The JIT
    aborts these (string LEN reads an array length field, so it must not compile;
    compare/concat are interpreter-only), so the JIT falls back and interp == jit.
    Guards the string-LEN-reads-garbage class. Deterministic."""
    n = r.choice([100000, 1000000])
    strs = ['"hello"', '"hi"', '"abcdef"', '"x"', '"test123"', '"ab"']
    s = r.choice(strs)
    op = r.choice(["len", "compare", "concat", "len_elem"])
    if op == "len":
        return f'string s = {s};\nint t = 0;\nfor (int i = 0, {n}) {{ t = t + #s; }}\nprint(t);\n'
    if op == "compare":
        s2 = r.choice(strs)
        return (f'string s = {s};\nint c = 0;\nfor (int i = 0, {n}) '
                f'{{ if (s == {s2}) {{ c = c + 1; }} else {{ c = c + 2; }} }}\nprint(c);\n')
    if op == "concat":
        ln = len(s) - 1  # chars without quotes, plus 1 for the appended "z"
        return (f'int c = 0;\nfor (int i = 0, {n}) {{ string a = {s}; string b = a .. "z"; '
                f'if (#b == {ln}) {{ c = c + 1; }} }}\nprint(c);\n')
    return (f'list<string> w = [{s}, "qq", "r"];\nint t = 0;\n'
            f'for (int i = 0, {n}) {{ t = t + #w[i % 3]; }}\nprint(t);\n')

def gen_array_len_mix(r):
    """Array element access combined with #v used as a VALUE in the same loop.
    This makes the compiler reuse the index register for the accumulator, so the
    element-type predictor sees a stale index slot at the back-edge -- it must
    fall back to element 0 and rely on the runtime bounds/type guards rather than
    aborting or mispredicting. Mixes int/float elements and read/write.
    Deterministic."""
    n = r.choice([200000, 1000000])
    sz = r.choice([3, 5, 8])
    if r.random() < 0.4:
        elems = ", ".join(f"{r.randint(1,9)}.5" for _ in range(sz))
        body = r.choice([
            f"s = s + v[i % {sz}] + (#v * 1.0);",
            f"v[i % {sz}] = i * 1.0; s = s + v[i % {sz}] + (#v * 1.0);",
        ])
        return (f"list<float> v = [{elems}];\nfloat s = 0.0;\n"
                f"for (int i = 0, {n}) {{ {body} }}\nprint(s);\n")
    elems = ", ".join(str(r.randint(1,9)) for _ in range(sz))
    body = r.choice([
        f"s = s + v[i % {sz}] + #v;",
        f"int m = #v; s = s + v[i % m] + m;",
        f"v[i % {sz}] = i; s = s + v[i % {sz}] + #v;",
    ])
    return (f"list<int> v = [{elems}];\nint s = 0;\n"
            f"for (int i = 0, {n}) {{ {body} }}\nprint(s);\n")

def gen_running_minmax(r):
    """Running max/min and kin: a loop-carried scalar that is BOTH a comparison
    operand AND conditionally reassigned to the other (unrelated) operand --
    `if (v > m) m = v;`. This exposed a register-residency bug (§10.24): m's
    register held its old value for the guard but was also assigned m's new value
    (v, computed earlier in the body), clobbering the old value before the guard
    read it. The branch distribution also flips after warmup, exercising the
    abort/phase-shift -> skip-path recording. Varies operator, init, modulus,
    int/float, and adds a second accumulator sometimes. Deterministic."""
    n = r.choice([200000, 1000000])
    md = r.choice([50, 100, 256])
    if r.random() < 0.35:
        op = r.choice([">", "<", ">=", "<="])
        init = r.choice(["0.0", "999999.0"])
        extra = r.choice(["", " s = s + v;"])
        return (f"float m = {init};\nfloat s = 0.0;\n"
                f"for (int i = 0, {n}) {{ float v = (i % {md}) * 1.0; "
                f"if (v {op} m) {{ m = v; }}{extra} }}\nprint(m + s);\n")
    op = r.choice([">", "<", ">=", "<="])
    init = r.choice(["0", "999999", str(r.randint(1, 99))])
    extra = r.choice(["", " s = s + v;", " s = s + 1;"])
    return (f"int m = {init};\nint s = 0;\n"
            f"for (int i = 0, {n}) {{ int v = i % {md}; "
            f"if (v {op} m) {{ m = v; }}{extra} }}\nprint(m * 100000 + s);\n")

def gen_ifconv(r):
    """Simple integer if-else / if-only conditional assignments -- the shape the
    JIT if-converts into a branchless select (B + (A-B)*cond). Exercises both
    arms evaluated unconditionally, the 0/1 comparison materialization, and the
    fallback when an arm is non-convertible. Mixes: if-else vs if-only, the
    comparison operator, clamp/abs/select-const/accumulate forms, and an
    occasional float arm (must fall back to the guarded branch). Deterministic."""
    n = r.choice([200000, 1000000])
    md = r.choice([7, 10, 21, 100, 200])
    op = r.choice([">", "<", ">=", "<=", "==", "!="])
    thr = r.randint(0, md - 1)
    form = r.randint(0, 9)
    if form == 6:  # max/min + index (two-slot if-only)
        big = r.choice([">", ">="]) if r.random() < 0.5 else r.choice(["<", "<="])
        init = "0" if big[0] == ">" else "999999999"
        return (f"int m = {init};\nint mi = 0;\nfor (int i = 0, {n}) {{ int v = i % {md}; "
                f"if (v {big} m) {{ m = v; mi = i; }} }}\nprint(m * 1000000 + mi % 1000000);\n")
    if form == 7:  # two carried, opposite directions (two-slot if-only)
        return (f"int x = 0;\nint y = 999999999;\nfor (int i = 0, {n}) {{ "
                f"if (x < y) {{ x = x + {r.randint(1,3)}; y = y - {r.randint(1,3)}; }} }}\n"
                f"print(x % 1000000 * 1000 + y % 1000);\n")
    if form == 8:  # two-slot accumulate in both arms (if-else)
        return (f"int a = 0;\nint b = 0;\nfor (int i = 0, {n}) {{ if (i % {md} {op} {thr}) "
                f"{{ a = a + {r.randint(1,4)}; b = b + {r.randint(1,4)}; }} else "
                f"{{ a = a + {r.randint(1,4)}; b = b + {r.randint(1,4)}; }} }}\n"
                f"print(a % 1000000 * 1000 + b % 1000);\n")
    if form == 9:  # three-slot update (if-only)
        return (f"int a = 0;\nint b = 0;\nint c = 0;\nfor (int i = 0, {n}) {{ int v = i % {md}; "
                f"if (v {op} {thr}) {{ a = a + 1; b = b + v; c = c + (i % 4); }} }}\n"
                f"print(a % 1000 * 1000000 + b % 1000 * 1000 + c % 1000);\n")
    if form == 0:  # if-else accumulate (50/50-style)
        a, b = r.randint(1, 5), r.randint(1, 5)
        return (f"int s = 0;\nfor (int i = 0, {n}) {{ if (i % {md} {op} {thr}) "
                f"{{ s = s + {a}; }} else {{ s = s + {b}; }} }}\nprint(s);\n")
    if form == 1:  # clamp (two if-only)
        lo, hi = r.randint(0, 20), r.randint(50, 150)
        return (f"int s = 0;\nfor (int i = 0, {n}) {{ int v = i % {md} - {md // 3}; "
                f"if (v < 0) {{ v = 0; }} if (v > {hi}) {{ v = {hi}; }} s = s + v; }}\nprint(s);\n")
    if form == 2:  # abs via unary minus (if-only)
        return (f"int s = 0;\nfor (int i = 0, {n}) {{ int v = i % {md} - {md // 2}; "
                f"if (v < 0) {{ v = -v; }} s = s + v; }}\nprint(s % 1000000000);\n")
    if form == 3:  # select constant (if-else)
        a, b = r.randint(1, 999), r.randint(1, 999)
        return (f"int s = 0;\nfor (int i = 0, {n}) {{ int v = i % {md}; "
                f"if (v {op} {thr}) {{ v = {a}; }} else {{ v = {b}; }} s = s + v; }}\nprint(s % 1000000000);\n")
    if form == 4:  # running max/min via move (if-only)
        init = r.choice(["0", "999999"])
        return (f"int m = {init};\nfor (int i = 0, {n}) {{ int v = i % {md}; "
                f"if (v {op} m) {{ m = v; }} }}\nprint(m);\n")
    # form 5: float arm -> must fall back to the guarded branch, still correct
    return (f"int s = 0;\nfor (int i = 0, {n}) {{ float t = (i % {md}) * 1.0; "
            f"if (i % {md} {op} {thr}) {{ s = s + 1; }} t = t + 1.0; }}\nprint(s);\n")

def gen_condreturn(r):
    """A conditional-return helper `if(c){return A} return B` called in a hot loop
    -- the shape the JIT inlines and if-converts to a branchless select. Exercises
    inlining + return if-conversion together: the callee frame fork, the shared
    CMPSET, binding the select to the caller's result slot. Mixes one/two args,
    the comparison operator, simple vs computed return values, an optional prefix
    computation in the condition, and occasionally a non-inlinable shape (a second
    branch, or a loop in the callee) that must cleanly fall back. Deterministic."""
    n = r.choice([200000, 1000000])
    md = r.choice([7, 10, 21, 50, 200])
    form = r.randint(0, 6)
    op = r.choice([">", "<", ">=", "<=", "==", "!="])
    thr = r.randint(0, md - 1)
    if form == 0:  # clamp-low
        return (f"function clamp(int x) {{ if (x < 0) {{ return 0; }} return x; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + clamp(i % {md} - {md//3}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 1:  # abs via 0-x
        return (f"function ab(int x) {{ if (x < 0) {{ return 0 - x; }} return x; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + ab(i % {md} - {md//2}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 2:  # max(a,b)
        return (f"function mx(int a, int b) {{ if (a > b) {{ return a; }} return b; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + mx(i % {md}, i % {max(2,md//2)}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 3:  # min(a,b)
        return (f"function mn(int a, int b) {{ if (a < b) {{ return a; }} return b; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + mn(i % {md}, i % {max(2,md//2)}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 4:  # computed return values, parameterized operator
        a, b = r.randint(1, 5), r.randint(1, 5)
        return (f"function f(int x) {{ if (x {op} {thr}) {{ return x * {a}; }} return x + {b}; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 5:  # second branch -> must fall back (not a single conditional return)
        return (f"function sg(int x) {{ if (x > 0) {{ return 1; }} if (x < 0) {{ return 0 - 1; }} return 0; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + sg(i % {md} - {md//2}); }}\n"
                f"print(s % 1000000000);\n")
    # form 6: loop in callee -> must fall back
    return (f"function f(int x) {{ int t = 0; for (int j = 0, 3) {{ t = t + j; }} "
            f"if (x {op} {thr}) {{ return t + x; }} return t; }}\n"
            f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md}); }}\n"
            f"print(s % 1000000000);\n")

def gen_condassign(r):
    """A conditional-assignment-then-return helper `if(c){slot=A} return slot`
    called in a hot loop -- the JIT inlines it and if-converts the assignment in
    the callee frame, then binds the trailing return to the caller's result.
    Mixes one/two args, the operator, simple vs computed assignments, an optional
    post-merge computation, and -- crucially -- the `0-x` form whose intra-arm
    dependency makes the assignment non-if-convertible, which MUST abort cleanly
    (fall back to the interpreter) rather than emit a broken mid-callee branch."""
    n = r.choice([200000, 1000000])
    md = r.choice([15, 21, 50, 100, 200])
    op = r.choice([">", "<", ">=", "<="])
    thr = r.randint(0, md - 1)
    form = r.randint(0, 6)
    if form == 0:  # clamp-low
        return (f"function f(int x) {{ if (x < 0) {{ x = 0; }} return x; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md} - {md//3}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 1:  # ceil-high
        return (f"function f(int x) {{ if (x > {md//2}) {{ x = {md//2}; }} return x; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 2:  # assign computed (x - const), single-op arm, if-convertible
        k = r.randint(1, 9)
        return (f"function f(int x) {{ if (x {op} {thr}) {{ x = x - {k}; }} return x; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 3:  # post-merge computation then return
        return (f"function f(int x) {{ if (x < 0) {{ x = 0; }} int y = x * 2 + 1; return y; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md} - {md//2}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 4:  # two-arg max-into-a
        return (f"function f(int a, int b) {{ if (a < b) {{ a = b; }} return a; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md}, i % {max(2,md//2)}); }}\n"
                f"print(s % 1000000000);\n")
    if form == 5:  # 0-x intra-arm dep -> MUST abort cleanly (correctness probe)
        return (f"function f(int x) {{ if (x < 0) {{ x = 0 - x; }} return x; }}\n"
                f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md} - {md//2}); }}\n"
                f"print(s % 1000000000);\n")
    # form 6: two-statement intra-dep arm -> MUST abort cleanly
    return (f"function f(int x) {{ if (x < 0) {{ int t = 0 - x; x = t + 1; }} return x; }}\n"
            f"int s = 0;\nfor (int i = 0, {n}) {{ s = s + f(i % {md} - {md//2}); }}\n"
            f"print(s % 1000000000);\n")

def gen_global_read(r):
    """Reading globals inside a hot loop: exercises OP_GETTABUP (ULOAD of the
    _ENV upvalue table + inline hash-slot lookup of a constant short-string key,
    lowered like GETFIELD). Declares a few int/float globals and combines them
    in a nested loop. The value type is predicted at record time and guarded at
    run time; here globals keep a fixed type so the fast path fires. GETTABUP is
    not RA-safe, so the trace runs with RA disabled. Output is deterministic and
    identical to the interpreter regardless of whether it JITs."""
    NAMES = ["g", "gi", "gf", "gx", "gy", "gz", "gk", "gn", "ga", "gb",
             "gc", "gq", "gw", "gv", "gh", "gl", "gm", "gp", "gr", "gs"]
    rep = r.choice([4000, 30000])
    inner = r.choice([7, 15])
    nint = r.randint(1, 3)
    flt = r.random() < 0.4
    names = r.sample(NAMES, nint + (1 if flt else 0))
    ints = names[:nint]
    decls = "".join(f"{nm} = {r.randint(1, 40)};\n" for nm in ints)
    lines = [decls, "int si = 0;\n"]
    # int expression combining the int globals
    iops = ["+", "-", "+", "*"]
    iexpr = ints[0]
    for nm in ints[1:]:
        iexpr += f" {r.choice(iops)} {nm}"
    body = [f"    si = si + ({iexpr});\n"]
    tail = ["print(si);\n"]
    if flt:
        gfn = names[nint]
        decls2 = f"{gfn} = {r.randint(1, 9)}.{r.randint(0, 9)};\n"
        lines[0] = decls2 + lines[0]
        lines.append("float sf = 0.0;\n")
        body.append(f"    sf = sf + {gfn};\n")
        tail.append("print(sf);\n")
    src = "".join(lines)
    src += f"for (int rep = 0, {rep}) {{\n  for (int j = 0, {inner}) {{\n"
    src += "".join(body)
    src += "  }\n}\n"
    src += "".join(tail)
    return src

def gen_math_call(r):
    """Unary math functions in a hot loop (math.sqrt/sin/cos/... via a direct
    libm C call, SPTIR_FMATH). Exercises the C-call mechanism, the GUARD_CFUNC
    method pin, and the int-arg TOFLT path. Several functions and both float and
    int argument lists are combined; results are transcendental but deterministic
    and identical to the interpreter (the run-time guards, not the record-time
    prediction, are load-bearing). The trace runs with RA disabled (FMATH is not
    RA-safe), and the C call is ABI-sensitive, so this is also a valgrind probe."""
    FNS = ["sqrt", "sin", "cos", "tan", "exp", "asin", "acos"]
    # asin/acos need |x|<=1; keep the float list in [0,1] and reserve sqrt/exp
    # for any-positive values. Use a [0,1) float list so every fn is well-defined.
    rep = r.choice([3000, 20000])
    inner = r.choice([7, 15])
    fa = ", ".join(f"0.{r.randint(0, 9)}{r.randint(1, 9)}" for _ in range(inner + 1))
    decls = [f"list<float> fa = [{fa}];", "float s = 0.0;"]
    use_int = r.random() < 0.5
    if use_int:
        ia = ", ".join(str(r.randint(1, 80)) for _ in range(inner + 1))
        decls.insert(1, f"list<int> ia = [{ia}];")
    # build the body: 1..3 math terms on fa[j], optionally one sqrt on ia[j]
    nterm = r.randint(1, 3)
    fns = [r.choice(FNS) for _ in range(nterm)]
    terms = [f"math.{fn}(fa[j])" for fn in fns]
    if use_int:
        terms.append("math.sqrt(ia[j])")   # int arg -> TOFLT before the call
    expr = " + ".join(terms)
    src = "\n".join(decls) + "\n"
    src += f"for (int rep = 0, {rep}) {{\n  for (int j = 0, {inner}) {{\n"
    src += f"    s = s + {expr};\n"
    src += "  }\n}\nprint(s);\n"
    return src

def gen_for_each(r):
    """Generic for-each over a List: `for k[,v] : pairs(L)` compiles to a native
    index loop (the OP_TFORCALL/OP_TFORLOOP specialization). The iterator step
    lowers to newkey = key + 1; GUARD newkey < #L; value = L[newkey] (a GETI) --
    no C call into luaB_next, no GC. Exercises the iterator entry guard (pinned
    to luaB_next), int/float element paths, key carrying, a branch in the body,
    and per-entry state reload (a different list each outer iteration). The
    loop-end exit re-runs the real TFORCALL so termination matches the
    interpreter exactly; for-each runs on the spill path (RA disabled because the
    loop-carried key is incremented in place)."""
    typ = r.choice(["int", "float"])
    if typ == "float":
        shape = r.choice(["value", "value_branch"])
    else:
        shape = r.choice(["value", "value_branch", "keyvalue", "key"])
    rep = r.choice([20000, 50000])
    def mklist(n):
        if typ == "int":
            return ", ".join(str(r.randint(-9, 30)) for _ in range(n))
        return ", ".join(f"{r.randint(1,40)}.{r.choice([0,25,5,75,125])}" for _ in range(n))
    decls = [f"list<{typ}> la = [{mklist(r.randint(1, 12))}];"]
    reassign = r.random() < 0.4
    if reassign:
        decls.append(f"list<{typ}> lb = [{mklist(r.randint(1, 12))}];")
    decls.append("int s = 0;" if typ == "int" else "float s = 0.0;")
    op = r.choice(["+", "-"]) if typ == "float" else r.choice(AOPS)
    need_sk = (shape == "keyvalue")
    if need_sk:
        decls.append("int sk = 0;")
    if shape == "value":
        loopvars, body = "k, v", f"s = s {op} v;"
    elif shape == "value_branch":
        thresh = r.randint(0, 10) if typ == "int" else f"{r.randint(0,20)}.0"
        loopvars, body = "k, v", f"if (v > {thresh}) {{ s = s {op} v; }} else {{ s = s + v; }}"
    elif shape == "keyvalue":
        loopvars, body = "k, v", f"s = s {op} v; sk = sk + k;"
    else:  # key only (1 loop var)
        loopvars, body = "k", "s = s + k;"
    src = "\n".join(decls) + "\n"
    src += f"for (int rep = 0, {rep}) {{\n"
    if reassign:
        src += f"  list<{typ}> cur = la;\n  if (rep >= {rep // 2}) {{ cur = lb; }}\n"
        target = "cur"
    else:
        target = "la"
    src += f"  for ({loopvars} : pairs({target})) {{ {body} }}\n}}\n"
    src += "print(s);\n"
    if need_sk:
        src += "print(sk);\n"
    return src

def gen_nested_container(r):
    """Chained container access rooted in a Map: m["k"][j] (map of list) and
    m["k1"]["k2"] (map of map), plus list-of-list a[i][j]. The element-type
    predictor (rec_eval_container) follows the IR SLOAD->GETFIELD->GETI/GETFIELD
    to the record-time inner container, since the intermediate List/Map is
    produced earlier in the trace and its live stack slot is stale by the
    back-edge; the base map of a chained GETFIELD is likewise IR-resolved. All
    loads stay bounds/type-guarded at run time (prediction is never
    load-bearing); no C call, no GC."""
    shape = r.choice(["mol_int", "mol_flt", "mom", "lol"])
    rep = r.choice([20000, 50000])
    if shape == "mol_int":
        sz = r.randint(1, 8)
        vals = ", ".join(str(r.randint(-9, 30)) for _ in range(sz))
        key = r.choice(["a", "b", "k", "x"]); op = r.choice(AOPS)
        return (f'map<string,list<int>> m = {{}};\nm["{key}"] = [{vals}];\nint s = 0;\n'
                f'for (int rep = 0, {rep}) {{ for (int j = 0, {sz-1}) {{ s = s {op} m["{key}"][j]; }} }}\nprint(s);\n')
    if shape == "mol_flt":
        sz = r.randint(1, 8)
        vals = ", ".join(f"{r.randint(1,40)}.{r.choice([0,25,5,75,125])}" for _ in range(sz))
        key = r.choice(["a", "v", "k"]); op = r.choice(["+", "-"])
        return (f'map<string,list<float>> m = {{}};\nm["{key}"] = [{vals}];\nfloat s = 0.0;\n'
                f'for (int rep = 0, {rep}) {{ for (int j = 0, {sz-1}) {{ s = s {op} m["{key}"][j]; }} }}\nprint(s);\n')
    if shape == "mom":
        k1 = r.choice(["p", "q", "a"]); vx = r.randint(-20, 50); vy = r.randint(-20, 50)
        inner = r.choice([7, 13]); op = r.choice(AOPS)
        return (f'map<string,map<string,int>> m = {{}};\nmap<string,int> inner = {{}};\n'
                f'inner["x"] = {vx};\ninner["y"] = {vy};\nm["{k1}"] = inner;\nint s = 0;\n'
                f'for (int rep = 0, {rep}) {{ for (int j = 0, {inner}) {{ s = s + m["{k1}"]["x"] {op} m["{k1}"]["y"]; }} }}\nprint(s);\n')
    rows = r.randint(1, 4); cols = r.randint(1, 4); op = r.choice(AOPS)
    rowstrs = ["[" + ", ".join(str(r.randint(-9, 20)) for _ in range(cols)) + "]" for _ in range(rows)]
    return (f'list<list<int>> a = [{", ".join(rowstrs)}];\nint s = 0;\n'
            f'for (int rep = 0, {rep}) {{ for (int i = 0, {rows-1}) {{ for (int j = 0, {cols-1}) {{ s = s {op} a[i][j]; }} }} }}\nprint(s);\n')

GENS = [gen_scalar,
        lambda r: gen_array_reduce(r, "int"),
        lambda r: gen_array_reduce(r, "float"),
        gen_cse_self, gen_two_array, gen_write_read, gen_chained2d, gen_branch,
        gen_moddiv, gen_float_moddiv,
        gen_multi_accum, gen_nested3, gen_bitwise_neg, gen_mixed_cmp,
        gen_inline_call, gen_swap, gen_copy_carry, gen_for_while, gen_const_fold, gen_float_bitwise, gen_type_transition, gen_string, gen_array_len_mix,
        gen_running_minmax, gen_ifconv, gen_condreturn, gen_condassign, gen_side_store, gen_unroll_nest, gen_recursive_nest, gen_speculative_nest, gen_map_access, gen_map_write, gen_global_read, gen_math_call, gen_for_each, gen_nested_container]

def run(bin_, src, env):
    with tempfile.NamedTemporaryFile("w", suffix=".spt", delete=False) as f:
        f.write(src); path = f.name
    try:
        e = dict(os.environ, **env)
        out = subprocess.run([bin_, path], capture_output=True, text=True,
                             env=e, timeout=40)
        return out.stdout.strip(), out.returncode
    except subprocess.TimeoutExpired:
        return "<TIMEOUT>", -1
    finally:
        os.unlink(path)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=300)
    ap.add_argument("--bin", default="./build/bin/sptscript")
    ap.add_argument("--hot", default="20")
    args = ap.parse_args()
    r = random.Random(args.seed)
    fails = []
    for i in range(args.count):
        src = r.choice(GENS)(r)
        # Vary the hot threshold per case. The old branch-recording coin-flip
        # (§10.22/§10.23) produced different traces at different thresholds, so
        # sweeping HOT here exercises that the majority-direction recording is
        # deterministic: the JIT result must equal the interpreter for *every*
        # threshold. Derived from the seeded RNG so runs stay reproducible.
        hot = str(r.randint(16, 40))
        off, rc0 = run(args.bin, src, {"SPT_JIT": "0"})
        on, rc1 = run(args.bin, src, {"SPT_JIT": "on", "SPT_JIT_HOT": hot})
        if off != on or rc0 != rc1:
            fails.append((i, src, off, on))
            print(f"--- MISMATCH #{i} (HOT={hot}, rc {rc0} vs {rc1}) ---")
            print(src)
            print(f"  off=[{off}]\n  on =[{on}]")
    print("=" * 60)
    print(f"seed={args.seed} count={args.count}: "
          f"{args.count - len(fails)} match, {len(fails)} MISMATCH")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()

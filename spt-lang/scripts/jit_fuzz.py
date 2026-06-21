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
    """String ops in a hot loop: length (#s), compare (==), concat (..). SHORT
    strings now compile #s to SLEN (§10.57, reads TString.shrlen + guards short);
    LONG strings, and #s inside an inlined method body, abort and fall back to the
    interpreter (the abort blacklists the PC -> bounded, never a compile thrash).
    Compare/concat stay interpreter-only. interp == jit either way. Guards the
    string-LEN class. Deterministic."""
    n = r.choice([100000, 1000000])
    strs = ['"hello"', '"hi"', '"abcdef"', '"x"', '"test123"', '"ab"']
    s = r.choice(strs)
    op = r.choice(["len", "compare", "concat", "len_elem", "len_long", "len_cond"])
    if op == "len":
        return f'string s = {s};\nint t = 0;\nfor (int i = 0, {n}) {{ t = t + #s; }}\nprint(t);\n'
    if op == "len_long":
        # a >40-char string: SLEN guard would fail every iteration, so recording
        # aborts (observably long) and blacklists -> interpreter, still bit-exact.
        lng = '"this is a deliberately long string of more than forty characters yes"'
        return f'string s = {lng};\nint t = 0;\nfor (int i = 0, {n}) {{ t = t + #s; }}\nprint(t);\n'
    if op == "len_cond":
        # #s used as a loop-relative bound / condition
        return (f'string s = {s};\nint t = 0;\n'
                f'for (int i = 0, {n}) {{ if (i % 16 < #s) {{ t = t + 1; }} }}\nprint(t);\n')
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

def gen_string_byte(r):
    """string.byte(s,i) and string.len(s) on a SHORT loop-invariant string in a hot
    loop (§10.58). string.byte lowers to SBYTE (reads TString content byte i, guards
    short + bounds), string.len reuses SLEN. Indices are kept IN RANGE (j < len) so
    the program never errors -- character hashing / byte summation / scanning, the
    real value of string support. Also emits a LONG-string shape (observably long ->
    recording aborts -> blacklist -> interpreter, still bit-exact, no thrash). The
    accumulated byte value is the wrong-byte / divergence detector. Deterministic."""
    shorts = ['"hello"', '"abcdef"', '"0123456789"', '"ABCDEFGH"', '"xyz"', '"Spt!"']
    s = r.choice(shorts)
    L = len(s) - 2                       # chars without the quotes
    n = r.choice([60000, 120000])
    shape = r.choice(["hash", "sum", "scan", "len", "stride", "long"])
    if shape == "hash":                  # rolling hash over the bytes
        k = r.choice([10, 11, 13, 31])
        return (f'str s = {s};\nint h = 0;\n'
                f'for (int rr = 0, {n}) {{ h = 0; for (int j = 0, {L-1}) {{ h = h * {k} + string.byte(s, j); }} }}\nprint(h);\n')
    if shape == "sum":                   # sum of all bytes
        return (f'str s = {s};\nint a = 0;\n'
                f'for (int rr = 0, {n}) {{ for (int j = 0, {L-1}) {{ a = a + string.byte(s, j); }} }}\nprint(a);\n')
    if shape == "scan":                  # guarded scan with #s as the bound
        return (f'str s = {s};\nint a = 0;\n'
                f'for (int rr = 0, {n}) {{ for (int j = 0, {L+3}) {{ if (j < #s) {{ a = a + string.byte(s, j) * (j + 1); }} }} }}\nprint(a);\n')
    if shape == "len":                   # string.len module function (-> SLEN)
        return (f'str s = {s};\nint a = 0;\n'
                f'for (int rr = 0, {n}) {{ for (int j = 0, 12) {{ a = a + string.len(s) + j; }} }}\nprint(a);\n')
    if shape == "stride":                # in-range index (j capped < len)
        m = max(1, (L - 1))
        return (f'str s = {s};\nint a = 0;\n'
                f'for (int rr = 0, {n}) {{ for (int j = 0, {m}) {{ a = a + string.byte(s, j); }} }}\nprint(a);\n')
    # long: a >40-char string -> SBYTE recording aborts (observably long) -> blacklist
    lng = '"this is a deliberately long string of more than forty characters yes ok"'
    return (f'str s = {lng};\nint a = 0;\n'
            f'for (int rr = 0, {n}) {{ for (int j = 0, 9) {{ a = a + string.byte(s, j); }} }}\nprint(a);\n')

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
    shape = r.choice(["mol_int", "mol_flt", "mom", "lol", "lom_read", "lom_write"])
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
    if shape == "lol":
        return (f'list<list<int>> a = [{", ".join(rowstrs)}];\nint s = 0;\n'
                f'for (int rep = 0, {rep}) {{ for (int i = 0, {rows-1}) {{ for (int j = 0, {cols-1}) {{ s = s {op} a[i][j]; }} }} }}\nprint(s);\n')
    # list<map<string,int>> element access: a[i]["k"] read or RMW
    n = r.randint(1, 5); key = r.choice(["v", "n", "k"])
    elems = ", ".join('{"' + key + '": ' + str(r.randint(-9, 30)) + '}' for _ in range(n))
    if shape == "lom_read":
        return (f'list<map<string,int>> a = [{elems}];\nint s = 0;\n'
                f'for (int rep = 0, {rep}) {{ for (int i = 0, {n-1}) {{ s = s {op} a[i]["{key}"]; }} }}\nprint(s);\n')
    # lom_write: RMW into each map element's field
    return (f'list<map<string,int>> a = [{elems}];\n'
            f'for (int rep = 0, {rep}) {{ for (int i = 0, {n-1}) {{ a[i]["{key}"] = a[i]["{key}"] + 1; }} }}\nprint(a[0]["{key}"]);\n')

def gen_method_call(r):
    """A class instance whose pure-read method (a getter / computed value over
    this-fields and params, no writes) is called in a hot loop -- exercises
    method inlining (OP_SELF + CALL) with resume-at-SELF guards. Bodies have no
    committed side effects, so interp and JIT must agree byte-for-byte; an
    out-of-bounds index or a varying (per-iteration) receiver must fall back to
    the interpreter rather than diverge. Ops are +/-/* (no div/mod), so no
    divide-by-zero regardless of the params."""
    shape = r.choice(["scalar_int", "scalar_float", "list_idx", "varying_recv",
                      "multi_arg", "zero_arg"])
    n = r.choice([100000, 400000])
    inner = r.randint(7, 19)
    o1, o2 = r.choice(AOPS), r.choice(AOPS)
    if shape == "scalar_int":
        a, b = r.randint(-9, 20), r.randint(1, 9)
        return (f"class C {{ int a; int b; void __init(){{ this.a = {a}; this.b = {b}; }} "
                f"int m(int v){{ return this.a {o1} v {o2} this.b; }} }}\n"
                f"C c = C();\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(i); }} }}\nprint(s);\n")
    if shape == "scalar_float":
        a, b = r.randint(0, 40) / 10.0, r.randint(1, 20) / 10.0
        return (f"class V {{ float a; float b; void __init(){{ this.a = {a}; this.b = {b}; }} "
                f"float m(int v){{ return this.a * v + this.b; }} }}\n"
                f"V v = V();\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + v.m(i); }} }}\nprint(s);\n")
    if shape == "list_idx":
        sz = r.randint(4, 9)
        vals = ", ".join(str(r.randint(-9, 30)) for _ in range(sz))
        return (f"class Buf {{ list<int> d; void __init(list<int> dd){{ this.d = dd; }} "
                f"int at(int i){{ return this.d[i] {o1} i; }} }}\n"
                f"Buf b = Buf([{vals}]);\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + b.at(i % {sz}); }} }}\nprint(s);\n")
    if shape == "varying_recv":
        k = r.randint(3, 6)
        objs = ", ".join(f"C({r.randint(-9, 20)})" for _ in range(k))
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} "
                f"int g(int p){{ return this.v {o1} p; }} }}\n"
                f"list<C> o = [{objs}];\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {k - 1}) {{ s = s + o[i].g(i); }} }}\nprint(s);\n")
    if shape == "zero_arg":
        a, b = r.randint(1, 12), r.randint(1, 12)
        return (f"class K {{ int a; int b; void __init(){{ this.a = {a}; this.b = {b}; }} "
                f"int g(){{ return this.a {o1} this.b; }} }}\n"
                f"K k = K();\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + k.g() + i; }} }}\nprint(s);\n")
    base = r.randint(1, 10)
    return (f"class W {{ int b; void __init(){{ this.b = {base}; }} "
            f"int f(int p, int q, int t){{ return this.b {o1} (p * q) {o2} t; }} }}\n"
            f"W w = W();\nint s = 0;\n"
            f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + w.f(i, 2, 1); }} }}\nprint(s);\n")

def gen_multi_method(r):
    """Several DISTINCT pure-read methods called on stable (loop-invariant)
    receivers in one hot loop -- exercises multiple-methods-per-trace inlining
    (each OP_SELF pins its own entry-guard identity; a repeat call to the same
    method reuses its entry). Real OOP shape `a.foo(); a.bar()`. All bodies are
    pure reads (no writes), so resume-at-SELF is valid and interp/JIT must agree
    byte-for-byte. Ops are +/-/* only (no div/mod), so no divide-by-zero. Each
    pattern calls 2-3 methods, optionally on 2 receivers, plus a dedup case."""
    shape = r.choice(["same_recv_2", "same_recv_3", "two_recv", "dedup", "float_2", "mixed_arg"])
    n = r.choice([100000, 300000])
    inner = r.randint(7, 19)
    o1, o2, o3 = r.choice(AOPS), r.choice(AOPS), r.choice(AOPS)
    if shape == "same_recv_2":
        a, b = r.randint(-9, 20), r.randint(1, 9)
        return (f"class C {{ int a; int b; void __init(){{ this.a = {a}; this.b = {b}; }} "
                f"int f(){{ return this.a {o1} 1; }} int g(int v){{ return this.b {o2} v; }} }}\n"
                f"C c = C();\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.f() + c.g(i); }} }}\nprint(s);\n")
    if shape == "same_recv_3":
        a, b = r.randint(-9, 20), r.randint(1, 9)
        return (f"class C {{ int a; int b; void __init(){{ this.a = {a}; this.b = {b}; }} "
                f"int f(){{ return this.a {o1} 1; }} int g(int v){{ return this.b {o2} v; }} "
                f"int h(){{ return this.a {o3} this.b; }} }}\n"
                f"C c = C();\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.f() + c.g(i) + c.h(); }} }}\nprint(s);\n")
    if shape == "two_recv":
        a, b = r.randint(-9, 20), r.randint(1, 12)
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} "
                f"int f(){{ return this.v {o1} 1; }} int g(int p){{ return this.v {o2} p; }} }}\n"
                f"C a = C({a});\nC b = C({b});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + a.f() + b.g(i); }} }}\nprint(s);\n")
    if shape == "dedup":
        a = r.randint(-9, 20)
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} int f(int p){{ return this.v {o1} p; }} }}\n"
                f"C c = C({a});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.f(i) + c.f(i {o2} 1); }} }}\nprint(s);\n")
    if shape == "float_2":
        a, b = r.randint(0, 40) / 10.0, r.randint(1, 20) / 10.0
        return (f"class V {{ float x; float y; void __init(float p, float q){{ this.x = p; this.y = q; }} "
                f"float scale(float k){{ return this.x * k; }} float sumf(){{ return this.x + this.y; }} }}\n"
                f"V v = V({a}, {b});\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + v.scale(2.0) + v.sumf(); }} }}\nprint(s);\n")
    base = r.randint(1, 10)
    off = r.randint(1, 9)
    return (f"class W {{ int b; void __init(){{ this.b = {base}; }} "
            f"int f(int p, int q, int t){{ return this.b {o1} (p * q) {o2} t; }} int k(){{ return this.b {o3} {off}; }} }}\n"
            f"W w = W();\nint s = 0;\n"
            f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + w.f(i, 2, 1) + w.k(); }} }}\nprint(s);\n")


def gen_condreturn_method(r):
    """A pure-read METHOD of shape `if (c) { return A } return B` whose arms read
    this-fields is inlined and its branch if-converted to a branchless select
    (clamp/abs/max/min/select-of-two-fields). Stable field-read guards keep it
    side-exit-free. Bodies have no writes, so any in-method guard resumes at SELF
    idempotently and interp/JIT must agree byte-for-byte. Float-field and
    variable-index variants must fall back (still correct), exercising the clean
    abort path. Ops are +/-/* only (no div/mod)."""
    shape = r.choice(["relu", "abs", "max_field", "min_field", "pick2",
                      "clamp_hi", "with_straightline", "float_fallback", "varidx_fallback"])
    n = r.choice([100000, 300000])
    inner = r.randint(7, 19)
    o1 = r.choice(AOPS)
    if shape == "relu":
        v = r.randint(-20, 20)
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} "
                f"int m(){{ if(this.v < 0) return 0; return this.v; }} }}\n"
                f"C c = C({v});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    if shape == "abs":
        v = r.randint(-20, 20)
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} "
                f"int m(){{ if(this.v < 0) return 0 - this.v; return this.v; }} }}\n"
                f"C c = C({v});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    if shape == "max_field":
        v = r.randint(-15, 15)
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} "
                f"int m(int p){{ if(this.v > p) return this.v; return p; }} }}\n"
                f"C c = C({v});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(i); }} }}\nprint(s);\n")
    if shape == "min_field":
        v = r.randint(-15, 15)
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} "
                f"int m(int p){{ if(this.v < p) return this.v; return p; }} }}\n"
                f"C c = C({v});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(i); }} }}\nprint(s);\n")
    if shape == "pick2":
        f0, a, b = r.randint(0, 1), r.randint(-30, 30), r.randint(-30, 30)
        return (f"class C {{ int f; int a; int b; void __init(int ff, int x, int y){{ this.f = ff; this.a = x; this.b = y; }} "
                f"int m(){{ if(this.f > 0) return this.a {o1} 1; return this.b; }} }}\n"
                f"C c = C({f0}, {a}, {b});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    if shape == "clamp_hi":
        v, hi = r.randint(-5, 200), r.randint(10, 100)
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} "
                f"int m(){{ if(this.v > {hi}) return {hi}; return this.v; }} }}\n"
                f"C c = C({v});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    if shape == "with_straightline":
        v = r.randint(-20, 20)
        return (f"class C {{ int v; void __init(int x){{ this.v = x; }} "
                f"int d(){{ return this.v {o1} this.v; }} int m(){{ if(this.v < 0) return 0; return this.v; }} }}\n"
                f"C c = C({v});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.d() + c.m(); }} }}\nprint(s);\n")
    if shape == "float_fallback":
        v = r.randint(-40, 40) / 10.0
        return (f"class C {{ float x; void __init(float a){{ this.x = a; }} "
                f"float m(){{ if(this.x < 0.0) return 0.0; return this.x; }} }}\n"
                f"C c = C({v});\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    sz = r.randint(4, 8)
    vals = ", ".join(str(r.randint(-9, 30)) for _ in range(sz))
    return (f"class C {{ list<int> d; int n; void __init(list<int> dd, int nn){{ this.d = dd; this.n = nn; }} "
            f"int m(int i){{ if(i < this.n) return this.d[i]; return -1; }} }}\n"
            f"C c = C([{vals}], {sz});\nint s = 0;\n"
            f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(i % {sz}); }} }}\nprint(s);\n")


def gen_write_method(r):
    """A single-trailing-write VOID method (setter/accumulator) inlined into a
    hot loop: reads this-fields, computes, then ONE field write as the last op,
    returns void. The accumulator sums are exact iff there is no double write
    (the core correctness property of resume-at-SELF with a trailing write).
    Multi-write and read-after-write methods (a guard after a committed write)
    must fall back to the interpreter, still correct. Ops are +/-/* (no div/mod);
    written values are int/float (no GC barrier)."""
    shape = r.choice(["accum", "accum_two", "setter", "scale", "compute_write",
                      "float_accum", "multiwrite_fallback", "readafterwrite_fallback"])
    n = r.choice([50000, 100000])
    inner = r.randint(6, 14)
    o1 = r.choice(AOPS)
    if shape == "accum":
        return ("class A { int t; void __init(){ this.t = 0; } void add(int x){ this.t = this.t + x; } int get(){ return this.t; } }\n"
                "A a = A();\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ a.add(i); }} }}\nprint(a.get());\n")
    if shape == "accum_two":
        return ("class A { int t; void __init(){ this.t = 0; } void add(int x){ this.t = this.t + x; } int get(){ return this.t; } }\n"
                "A a = A();\nA b = A();\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ a.add(i); b.add(i {o1} 1); }} }}\nprint(a.get());\nprint(b.get());\n")
    if shape == "setter":
        init = r.randint(-9, 9)
        return (f"class B {{ int v; void __init(int x){{ this.v = x; }} void set(int x){{ this.v = x; }} int get(){{ return this.v; }} }}\n"
                f"B b = B({init});\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ b.set(i); s = s + b.get(); }} }}\nprint(s);\nprint(b.get());\n")
    if shape == "scale":
        return ("class B { int v; void __init(){ this.v = 1; } void mul(int k){ this.v = this.v * k; } void reset(int x){ this.v = x; } int get(){ return this.v; } }\n"
                "B b = B();\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 1, 3) {{ b.reset(i); b.mul(2); s = s + b.get(); }} }}\nprint(s);\n")
    if shape == "compute_write":
        c = r.randint(1, 5)
        return (f"class B {{ int v; void __init(){{ this.v = 0; }} void step(int x){{ this.v = this.v {o1} (x {AOPS[r.randint(0,2)]} {c}); }} int get(){{ return this.v; }} }}\n"
                f"B b = B();\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ b.step(i); }} }}\nprint(b.get());\n")
    if shape == "float_accum":
        d = r.randint(1, 30) / 10.0
        return (f"class F {{ float f; void __init(){{ this.f = 0.0; }} void add(float x){{ this.f = this.f + x; }} float get(){{ return this.f; }} }}\n"
                f"F f = F();\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ f.add({d}); }} }}\nprint(f.get());\n")
    if shape == "multiwrite_fallback":
        return ("class T { int a; int b; void __init(){ this.a = 0; this.b = 0; } void bump(){ this.a = this.a + 1; this.b = this.b + 2; } int sum(){ return this.a + this.b; } }\n"
                "T t = T();\nint s = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ t.bump(); s = s + t.sum(); }} }}\nprint(s);\nprint(t.sum());\n")
    return ("class W { int v; void __init(){ this.v = 0; } int addret(int x){ this.v = this.v + x; return this.v; } }\n"
            "W w = W();\nint s = 0;\n"
            f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + w.addret(i); }} }}\nprint(s);\n")


def gen_condreturn_float(r):
    """A FLOAT conditional-return leaf (method or free function) `if(c){return A}
    return B` inlined and if-converted to a bit-exact branchless float select
    (FCMPMASK/ICMPMASK + FSELECT). Covers relu/clamp/abs/min/max with all four
    compare predicates (< > >= <=, the latter two compiled as swapped LT/LE so
    NaN stays false). NaN/+inf/-inf inputs are exercised. Mixed-type arms and
    int-arms-under-a-float-compare must fall back (still correct)."""
    shape = r.choice(["relu", "clamp_hi", "abs", "max_p", "min_p", "ge_branch",
                      "free_clamp", "free_min", "nan_inputs", "mixed_fallback"])
    n = r.choice([50000, 100000])
    inner = r.randint(6, 13)
    # base values, occasionally special (inf/nan/-inf)
    def fval():
        pick = r.random()
        if pick < 0.12:  return "(1.0e308 * 10.0)"         # +inf
        if pick < 0.20:  return "(1.0e308 * 10.0 - 1.0e308 * 10.0)"  # NaN
        if pick < 0.28:  return "(0.0 - 1.0e308 * 10.0)"   # -inf
        return f"{r.randint(-40,40)/10.0}"
    v = fval()
    hi = r.randint(5, 30) / 10.0
    if shape == "relu":
        return (f"class C {{ float x; void __init(float a){{ this.x = a; }} float m(){{ if(this.x < 0.0) return 0.0; return this.x; }} }}\n"
                f"C c = C({v});\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    if shape == "clamp_hi":
        return (f"class C {{ float x; void __init(float a){{ this.x = a; }} float m(){{ if(this.x > {hi}) return {hi}; return this.x; }} }}\n"
                f"C c = C({v});\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    if shape == "abs":
        return (f"class C {{ float x; void __init(float a){{ this.x = a; }} float m(){{ if(this.x < 0.0) return 0.0 - this.x; return this.x; }} }}\n"
                f"C c = C({v});\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    if shape == "max_p":
        p = r.randint(-30, 30) / 10.0
        return (f"class C {{ float x; void __init(float a){{ this.x = a; }} float m(float p){{ if(this.x > p) return this.x; return p; }} }}\n"
                f"C c = C({v});\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m({p}); }} }}\nprint(s);\n")
    if shape == "min_p":
        p = r.randint(-30, 30) / 10.0
        return (f"class C {{ float x; void __init(float a){{ this.x = a; }} float m(float p){{ if(this.x < p) return this.x; return p; }} }}\n"
                f"C c = C({v});\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m({p}); }} }}\nprint(s);\n")
    if shape == "ge_branch":
        return (f"class C {{ float x; void __init(float a){{ this.x = a; }} float m(){{ if(this.x >= 0.0) return this.x; return 0.0; }} }}\n"
                f"C c = C({v});\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")
    if shape == "free_clamp":
        return ("float clampf(float x){ if(x < 0.0) return 0.0; return x; }\n"
                f"float v = {v};\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + clampf(v); }} }}\nprint(s);\n")
    if shape == "free_min":
        a, b = fval(), fval()
        return ("float minf(float a, float b){ if(a < b) return a; return b; }\n"
                f"float p = {a};\nfloat q = {b};\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + minf(p, q); }} }}\nprint(s);\n")
    if shape == "nan_inputs":
        return ("class C { float x; void __init(float a){ this.x = a; } "
                "float relu(){ if(this.x < 0.0) return 0.0; return this.x; } "
                "float clamphi(){ if(this.x > 1.0) return 1.0; return this.x; } "
                "float le(){ if(this.x <= 0.0) return this.x; return 0.0; } }\n"
                "float nan = 1.0e308 * 10.0 - 1.0e308 * 10.0;\nfloat inf = 1.0e308 * 10.0;\n"
                "C a = C(nan); C b = C(inf); C d = C(0.0 - inf);\nfloat s = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + a.relu() + b.clamphi() + d.le(); }} }}\nprint(s);\n")
    # mixed_fallback: int arm vs float arm -> must abort cleanly, stay correct
    return (f"class C {{ float x; int n; void __init(float a, int b){{ this.x = a; this.n = b; }} "
            f"float m(){{ if(this.n < 0) return 0; return this.x; }} }}\n"
            f"C c = C({v}, {r.randint(-3,3)});\nfloat s = 0.0;\n"
            f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ s = s + c.m(); }} }}\nprint(s);\n")


def gen_condwrite_method(r):
    """An if-only conditional field write `if(c) this.f = A;` inlined and if-
    converted to one unconditional trailing write of a branchless select
    this.f = select(c, A, old). The unchanged value `old` is sourced either from a
    COMPARE operand (`if(x>this.peak) this.peak=x` -- running max/min, in-place
    clamp, cap) or, for conditional accumulation where the condition is on a
    different value, from the THEN-ARM's own read of the field
    (`if(x>0) this.sum=this.sum+x` -- sum-positives, conditional counters; §10.53).
    Covers int + float (FSELECT, NaN-as-false). Also handles if-ELSE conditional
    writes `if(c) this.f=A; else this.f=B` (both arms write the same field -> one
    select(c,A,B); §10.54), and conditional writes where `old` is read NOWHERE --
    cross-field `if(c) this.a=this.b+x`, conditional constant `if(x>0) this.v=5`,
    field copy `if(c) this.a=this.b` -- via a fresh guarded read of the written
    field for old (§10.55). Also emits the safety boundary -- multi-write,
    value-returning (read-after-write), if-else writing DIFFERENT fields -- which
    must abort cleanly yet stay bit-exact. The summed field value is the
    double-write / wrong-select detector."""
    shape = r.choice(["max", "min", "clamp_low", "cap", "float_max", "float_min",
                      "rmw_sum", "rmw_count", "rmw_scale", "float_rmw_sum",
                      "ifelse_sign", "ifelse_pick", "ifelse_compute", "ifelse_float", "ifelse_cmpfld",
                      "xfield_rmw", "constw_unrel", "fieldcpy", "float_xfield", "cmp_other",
                      "two_writes", "read_after_write", "ifelse_difffield"])
    n = r.choice([60000, 120000])
    inner = r.randint(8, 22)
    seed0 = r.randint(-30, 30)
    if shape == "max":
        return (f"class C {{ int peak; void __init(){{ this.peak = {seed0-50}; }} void track(int x){{ if(x > this.peak) this.peak = x; }} int get(){{ return this.peak; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.track((j * 3 + r) % 60 - 30); }} acc = acc + c.get(); }}\nprint(acc);\n")
    if shape == "min":
        return (f"class C {{ int low; void __init(){{ this.low = {seed0+50}; }} void track(int x){{ if(x < this.low) this.low = x; }} int get(){{ return this.low; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.track((j * 5 + r) % 60 - 30); }} acc = acc + c.get(); }}\nprint(acc);\n")
    if shape == "clamp_low":
        return (f"class C {{ int v; void __init(int a){{ this.v = a; }} void low(){{ if(this.v < 0) this.v = 0; }} int get(){{ return this.v; }} }}\n"
                f"int acc = 0;\n"
                f"for (int r = 0, {n}) {{ C c = C({seed0} - (r % {r.randint(7,25)})); for (int j = 0, {inner}) {{ c.low(); }} acc = acc + c.get(); }}\nprint(acc);\n")
    if shape == "cap":
        m = r.randint(20, 80)
        return (f"class C {{ int v; void __init(int a){{ this.v = a; }} void cap(int m){{ if(this.v > m) this.v = m; }} int get(){{ return this.v; }} }}\n"
                f"int acc = 0;\n"
                f"for (int r = 0, {n}) {{ C c = C({m+50} - (r % {r.randint(10,40)})); for (int j = 0, {inner}) {{ c.cap({m}); }} acc = acc + c.get(); }}\nprint(acc);\n")
    if shape == "float_max":
        start = r.choice(["-1.0e30", "(1.0e308 * 10.0 - 1.0e308 * 10.0)", "(0.0 - 1.0e308 * 10.0)", "0.0"])
        return (f"class C {{ float peak; void __init(float v){{ this.peak = v; }} void track(float x){{ if(x > this.peak) this.peak = x; }} float get(){{ return this.peak; }} }}\n"
                f"C c = C({start}); float acc = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.track(0.25 * j - 3.0); }} acc = acc + c.get(); }}\nprint(acc);\n")
    if shape == "float_min":
        start = r.choice(["1.0e30", "(1.0e308 * 10.0 - 1.0e308 * 10.0)", "(1.0e308 * 10.0)"])
        return (f"class C {{ float low; void __init(float v){{ this.low = v; }} void track(float x){{ if(x < this.low) this.low = x; }} float get(){{ return this.low; }} }}\n"
                f"C c = C({start}); float acc = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.track(3.0 - 0.25 * j); }} acc = acc + c.get(); }}\nprint(acc);\n")
    if shape == "rmw_sum":      # §10.53: conditional accumulation, old from then-arm read
        thr = r.randint(-5, 5)
        return (f"class S {{ int sum; void __init(){{ this.sum = 0; }} void add(int x){{ if(x > {thr}) this.sum = this.sum + x; }} int g(){{ return this.sum; }} }}\n"
                f"S s = S(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ s.add((j * 3 + r) % 40 - 20); }} acc = acc + s.g(); }}\nprint(acc);\n")
    if shape == "rmw_count":    # §10.53: conditional counter (this.n += 1 under a condition)
        k = r.randint(2, 12)
        return (f"class C {{ int n; void __init(){{ this.n = 0; }} void see(int x){{ if(x >= {k}) this.n = this.n + 1; }} int g(){{ return this.n; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.see((j + r) % 20); }} acc = acc + c.g(); }}\nprint(acc);\n")
    if shape == "rmw_scale":    # §10.53: conditional multiply (old from then-arm read), reset to bound growth
        return (f"class V {{ int v; void __init(){{ this.v = 1; }} void grow(int x){{ if(x > 0) this.v = this.v + x; }} void rst(int a){{ this.v = a; }} int g(){{ return this.v; }} }}\n"
                f"V v = V(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ v.rst(1); for (int j = 0, {inner}) {{ v.grow(j % 3 - 1); }} acc = acc + v.g(); }}\nprint(acc);\n")
    if shape == "float_rmw_sum":  # §10.53: float conditional accumulation (FSELECT, old from then-arm)
        return (f"class S {{ float sum; void __init(){{ this.sum = 0.0; }} void add(float x){{ if(x > 0.0) this.sum = this.sum + x; }} float g(){{ return this.sum; }} }}\n"
                f"S s = S(); float acc = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ s.add(0.5 * j - 3.0); }} acc = acc + s.g(); }}\nprint(acc);\n")
    if shape == "ifelse_sign":   # §10.54 if-else: constant/constant (sign)
        return (f"class C {{ int sign; void __init(){{ this.sign = 0; }} void cls(int x){{ if(x > 0) this.sign = 1; else this.sign = 0 - 1; }} int g(){{ return this.sign; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.cls((j + r) % 21 - 10); acc = acc + c.g(); }} }}\nprint(acc);\n")
    if shape == "ifelse_pick":   # §10.54 if-else: register/constant (max-like)
        k = r.randint(2, 9)
        return (f"class D {{ int v; void __init(){{ this.v = 0; }} void pick(int x){{ if(x > {k}) this.v = x; else this.v = {k}; }} int g(){{ return this.v; }} }}\n"
                f"D d = D(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ d.pick((j * 3 + r) % 20); acc = acc + d.g(); }} }}\nprint(acc);\n")
    if shape == "ifelse_compute":  # §10.54 if-else: both arms compute
        return (f"class E {{ int v; void __init(){{ this.v = 0; }} void f(int x){{ if(x > 3) this.v = x + 1; else this.v = x - 1; }} int g(){{ return this.v; }} }}\n"
                f"E e = E(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ e.f((j + r) % 12); acc = acc + e.g(); }} }}\nprint(acc);\n")
    if shape == "ifelse_float":  # §10.54 if-else: float (abs-like, FSELECT)
        return (f"class F {{ float v; void __init(){{ this.v = 0.0; }} void f(float x){{ if(x > 0.0) this.v = x; else this.v = 0.0 - x; }} float g(){{ return this.v; }} }}\n"
                f"F f = F(); float acc = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ f.f(0.5 * j - 4.0); acc = acc + f.g(); }} }}\nprint(acc);\n")
    if shape == "ifelse_cmpfld":  # §10.54 if-else: condition reads the written field
        return (f"class G {{ int v; void __init(){{ this.v = 50; }} void f(int x){{ if(this.v > x) this.v = x; else this.v = 0; }} int g(){{ return this.v; }} }}\n"
                f"G gg = G(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ gg.f((j + r) % 60); }} acc = acc + gg.g(); gg.f(999); }}\nprint(acc);\n")
    if shape == "xfield_rmw":   # §10.55: cross-field RMW (this.a = this.b + x), old from fresh read
        return (f"class C {{ int a; int b; void __init(){{ this.a = 0; this.b = {r.randint(1,9)}; }} void f(int x){{ if(x > 0) this.a = this.b + x; }} int g(){{ return this.a; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.f((j * 3 + r) % 30 - 15); }} acc = acc + c.g(); }}\nprint(acc);\n")
    if shape == "constw_unrel":  # §10.55: conditional constant write, unrelated condition
        v = r.randint(2, 40)
        return (f"class C {{ int v; void __init(){{ this.v = 0; }} void f(int x){{ if(x > 0) this.v = {v}; }} int g(){{ return this.v; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.f((j + r) % 12 - 6); acc = acc + c.g(); }} }}\nprint(acc);\n")
    if shape == "fieldcpy":     # §10.55: conditional field copy (this.a = this.b)
        return (f"class C {{ int a; int b; void __init(){{ this.a = 0; this.b = {r.randint(3,30)}; }} void f(int x){{ if(x > 0) this.a = this.b; }} int g(){{ return this.a; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.f((j + r) % 10 - 5); acc = acc + c.g(); }} }}\nprint(acc);\n")
    if shape == "float_xfield":  # §10.55: float cross-field RMW (FSELECT, old from fresh read)
        return (f"class C {{ float a; float b; void __init(){{ this.a = 0.0; this.b = 2.5; }} void f(float x){{ if(x > 0.0) this.a = this.b + x; }} float g(){{ return this.a; }} }}\n"
                f"C c = C(); float acc = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.f(0.5 * j - 4.0); }} acc = acc + c.g(); }}\nprint(acc);\n")
    if shape == "cmp_other":    # §10.55: condition reads a DIFFERENT field, then-arm writes param -> old from fresh read
        return (f"class C {{ int v; int g; void __init(){{ this.v=0; this.g={r.randint(3,20)}; }} void f(int x){{ if(x > this.g) this.v = x; }} int get(){{ return this.v; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.f(j); }} acc = acc + c.get(); }}\nprint(acc);\n")
    if shape == "two_writes":   # MUST abort (two SETFIELDs, if-only) -> still correct
        return (f"class C {{ int a; int b; void __init(){{ this.a=0; this.b=0; }} void f(int x){{ if(x > this.a) {{ this.a = x; this.b = x; }} }} int g(){{ return this.a + this.b; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.f((j + r) % 40); }} acc = acc + c.g(); }}\nprint(acc);\n")
    if shape == "read_after_write":  # MUST abort (value return) -> still correct
        return (f"class C {{ int v; void __init(){{ this.v=0; }} int f(int x){{ if(x > this.v) this.v = x; return this.v; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ acc = acc + c.f((j + r) % 40); }} }}\nprint(acc);\n")
    # ifelse_difffield: MUST abort (if-else writing DIFFERENT fields) -> still correct
    return (f"class C {{ int a; int b; void __init(){{ this.a=0; this.b=0; }} void f(int x){{ if(x > 0) this.a = 1; else this.b = 2; }} int get(){{ return this.a + this.b; }} }}\n"
            f"C c = C(); int acc = 0;\n"
            f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ c.f(j - {r.randint(1,8)}); }} acc = acc + c.get(); }}\nprint(acc);\n")


def gen_chained_condreturn(r):
    """Chained conditional-return (>=2 cond-returns + a final return) -- the
    double-sided clamp `if(x<lo) return lo; if(x>hi) return hi; return x`, inlined
    and folded to a NESTED branchless select (§10.56). Covers free-function and
    method forms, register and immediate (LTI/GTI) bounds, int (rounding-free
    integer select) and float (FSELECT) arms, and >2-deep chains. Also emits a
    mixed-type chain that MUST abort cleanly (one int arm + one float arm) yet stay
    bit-exact. The summed clamped value is the wrong-fold detector."""
    shape = r.choice(["free_reg", "method", "imm", "float", "triple", "mixed_abort"])
    n = r.choice([60000, 120000])
    inner = r.randint(10, 22)
    lo = r.randint(-5, 5); hi = lo + r.randint(5, 30)
    if shape == "free_reg":     # free-fn clamp, register bounds (OP_LT both sides)
        return (f"int clamp(int x, int lo, int hi){{ if(x < lo) return lo; if(x > hi) return hi; return x; }}\n"
                f"int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ acc = acc + clamp(j * {r.randint(1,4)} - {r.randint(5,20)}, {lo}, {hi}); }} }}\nprint(acc);\n")
    if shape == "method":       # method clamp reading this.lo/this.hi
        return (f"class C {{ int lo; int hi; void __init(int l, int h){{ this.lo = l; this.hi = h; }} int clamp(int x){{ if(x < this.lo) return this.lo; if(x > this.hi) return this.hi; return x; }} }}\n"
                f"C c = C({lo}, {hi}); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ acc = acc + c.clamp(j * {r.randint(1,4)} - {r.randint(5,20)}); }} }}\nprint(acc);\n")
    if shape == "imm":          # immediate bounds (LTI/GTI)
        a = r.randint(1, 6); b = a + r.randint(4, 20)
        return (f"int clamp(int x){{ if(x < {a}) return {a}; if(x > {b}) return {b}; return x; }}\n"
                f"int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ acc = acc + clamp(j * {r.randint(1,3)} - {r.randint(0,8)}); }} }}\nprint(acc);\n")
    if shape == "float":        # float clamp (FSELECT)
        return (f"float clampf(float x, float lo, float hi){{ if(x < lo) return lo; if(x > hi) return hi; return x; }}\n"
                f"float acc = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ acc = acc + clampf(0.5 * j - {r.randint(1,5)}.0, 0.0, {r.randint(3,8)}.0); }} }}\nprint(acc);\n")
    if shape == "triple":       # 3 cond-returns + final
        a = r.randint(-2, 2); b = a + r.randint(20, 40); m = a + r.randint(8, 15)
        return (f"int f(int x){{ if(x < {a}) return {a}; if(x > {b}) return {b}; if(x > {m}) return {m}; return x; }}\n"
                f"int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ acc = acc + f(j * {r.randint(2,5)} - {r.randint(2,10)}); }} }}\nprint(acc);\n")
    # mixed_abort: int arm + float arm in one chain -> must abort, stay correct
    return (f"float g(int x){{ if(x < 0) return 0; if(x > 10) return 1.0; return 0.5; }}\n"
            f"float acc = 0.0;\n"
            f"for (int r = 0, {n}) {{ for (int j = 0, {inner}) {{ acc = acc + g(j - {r.randint(1,6)}); }} }}\nprint(acc);\n")


def gen_writeret_method(r):
    """A method that writes ONE field then returns that SAME field
    (`int inc(){ this.v = this.v + 1; return this.v; }`) -- the increment/
    accumulate/set-and-return-new-value pattern (counters, ID generators, running
    totals). The post-write re-read of the written field is store-to-load
    forwarded to the written value (guard-free), so the SETFIELD stays the last
    guard-emitting op and resume-at-SELF never double-writes. The accumulated sum
    of the RETURNED values is exact iff forwarding returns the right value AND
    there is no double write -- both are what we check. Safety boundaries that
    MUST fall back (a guard would follow the write): writing one field then
    returning a DIFFERENT field, and multi-write. All stay bit-exact."""
    shape = r.choice(["inc", "addret", "nextid", "setret", "compute_ret",
                      "cross_field", "float_addret",
                      "readother_fallback", "multiwrite_fallback"])
    n = r.choice([50000, 100000])
    inner = r.randint(6, 14)
    o1 = r.choice(AOPS)
    if shape == "inc":          # counter: return ++this.v
        init = r.randint(-5, 5)
        return (f"class A {{ int v; void __init(int x){{ this.v = x; }} int inc(){{ this.v = this.v + 1; return this.v; }} }}\n"
                f"A a = A({init}); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ acc = acc + a.inc(); }} }}\nprint(acc);\nprint(a.inc());\n")
    if shape == "addret":       # running total: this.v += x; return this.v
        d = r.randint(1, 4)
        return (f"class A {{ int v; void __init(){{ this.v = 0; }} int add(int x){{ this.v = this.v + x; return this.v; }} }}\n"
                f"A a = A(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ acc = acc + a.add({d}); }} }}\nprint(acc);\n")
    if shape == "nextid":       # ID generator: this.id += 1; return this.id
        return (f"class G {{ int id; void __init(){{ this.id = 0; }} int next(){{ this.id = this.id + 1; return this.id; }} }}\n"
                f"G g = G(); int last = 0; int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ last = g.next(); acc = acc {o1} last; }} }}\nprint(last);\nprint(acc);\n")
    if shape == "setret":       # set a param, return it: this.v = x; return this.v
        return (f"class A {{ int v; void __init(){{ this.v = 0; }} int set(int x){{ this.v = x; return this.v; }} }}\n"
                f"A a = A(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ acc = acc + a.set(i {o1} 1); }} }}\nprint(acc);\nprint(a.set(7));\n")
    if shape == "compute_ret":  # this.v = this.v OP (x OP c); return this.v
        c = r.randint(1, 5)
        return (f"class A {{ int v; void __init(){{ this.v = 0; }} int step(int x){{ this.v = this.v {o1} (x {AOPS[r.randint(0,2)]} {c}); return this.v; }} }}\n"
                f"A a = A(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ acc = acc + a.step(i); }} }}\nprint(acc);\n")
    if shape == "cross_field":  # write a from b, return a (forward across fields)
        b0 = r.randint(1, 6)
        return (f"class C {{ int a; int b; void __init(){{ this.a = 0; this.b = {b0}; }} int f(){{ this.a = this.b + 1; return this.a; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ acc = acc + c.f(); }} }}\nprint(acc);\n")
    if shape == "float_addret": # float running total: this.f += x; return this.f
        d = r.randint(1, 30) / 10.0
        return (f"class F {{ float f; void __init(){{ this.f = 0.0; }} float add(float x){{ this.f = this.f + x; return this.f; }} }}\n"
                f"F f = F(); float acc = 0.0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ acc = acc + f.add({d}); }} }}\nprint(acc);\n")
    if shape == "readother_fallback":  # write a, return DIFFERENT field b -> MUST abort, still correct
        b0 = r.randint(2, 9)
        return (f"class C {{ int a; int b; void __init(){{ this.a = 0; this.b = {b0}; }} int f(int x){{ this.a = x; return this.b; }} }}\n"
                f"C c = C(); int acc = 0;\n"
                f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ acc = acc + c.f(i); }} }}\nprint(acc);\nprint(c.a);\n")
    # multiwrite_fallback: two writes then return -> MUST abort, still correct
    return (f"class T {{ int a; int b; void __init(){{ this.a = 0; this.b = 0; }} int bump(){{ this.a = this.a + 1; this.b = this.b + 2; return this.a; }} }}\n"
            f"T t = T(); int acc = 0;\n"
            f"for (int r = 0, {n}) {{ for (int i = 0, {inner}) {{ acc = acc + t.bump(); }} }}\nprint(acc);\nprint(t.b);\n")


GENS = [gen_scalar,
        lambda r: gen_array_reduce(r, "int"),
        lambda r: gen_array_reduce(r, "float"),
        gen_cse_self, gen_two_array, gen_write_read, gen_chained2d, gen_branch,
        gen_moddiv, gen_float_moddiv,
        gen_multi_accum, gen_nested3, gen_bitwise_neg, gen_mixed_cmp,
        gen_inline_call, gen_swap, gen_copy_carry, gen_for_while, gen_const_fold, gen_float_bitwise, gen_type_transition, gen_string, gen_array_len_mix,
        gen_running_minmax, gen_ifconv, gen_condreturn, gen_condassign, gen_side_store, gen_unroll_nest, gen_recursive_nest, gen_speculative_nest, gen_map_access, gen_map_write, gen_global_read, gen_math_call, gen_for_each, gen_nested_container, gen_method_call, gen_multi_method, gen_condreturn_method, gen_write_method, gen_condreturn_float, gen_condwrite_method, gen_writeret_method, gen_chained_condreturn, gen_string_byte]

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

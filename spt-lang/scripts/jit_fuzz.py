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

GENS = [gen_scalar,
        lambda r: gen_array_reduce(r, "int"),
        lambda r: gen_array_reduce(r, "float"),
        gen_cse_self, gen_two_array, gen_write_read, gen_chained2d, gen_branch,
        gen_moddiv, gen_float_moddiv,
        gen_multi_accum, gen_nested3, gen_bitwise_neg, gen_mixed_cmp,
        gen_inline_call, gen_swap, gen_copy_carry, gen_for_while, gen_const_fold]

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
        off, rc0 = run(args.bin, src, {"SPT_JIT": "0"})
        on, rc1 = run(args.bin, src, {"SPT_JIT": "on", "SPT_JIT_HOT": args.hot})
        if off != on or rc0 != rc1:
            fails.append((i, src, off, on))
            print(f"--- MISMATCH #{i} (rc {rc0} vs {rc1}) ---")
            print(src)
            print(f"  off=[{off}]\n  on =[{on}]")
    print("=" * 60)
    print(f"seed={args.seed} count={args.count}: "
          f"{args.count - len(fails)} match, {len(fails)} MISMATCH")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()

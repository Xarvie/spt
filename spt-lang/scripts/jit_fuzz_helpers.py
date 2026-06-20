#!/usr/bin/env python3
"""Aggressive differential fuzzer targeting the inlining + if-conversion machinery.

Motivated by the §10.28 latent mid-callee bug: the standard fuzzer's generators
don't exercise the fallback/abort paths or the feature *interactions* (inlined
helper x caller-side if-conversion x loop-carried accumulators x float args x
nested/duplicate calls). This generator builds "helper-zoo" programs: a handful
of helper functions of diverse shapes (pure leaf, conditional return, conditional
assignment, two-arg, and deliberately non-inlinable / abort-inducing ones), then
a hot loop that calls them in varied ways. Runs each JIT-off vs JIT-on across
several HOT thresholds; any output mismatch (or a JIT crash producing empty/short
output) is a real bug.
"""
import subprocess, os, sys, random, time

CMP = [">", "<", ">=", "<=", "==", "!="]

def helper(r, name, kind, ftype):
    """Emit one helper definition of the given kind. ftype is 'int' or 'float'."""
    t = ftype
    z = "0.0" if t == "float" else "0"
    one = "1.0" if t == "float" else "1"
    k = (f"{r.randint(1,9)}.0" if t == "float" else f"{r.randint(1,9)}")
    thr = (f"{r.randint(0,9)}.0" if t == "float" else f"{r.randint(0,9)}")
    cmp = r.choice(CMP)
    if kind == "leaf":
        op = r.choice(["+", "-", "*"])
        return f"function {name}({t} x) {{ return x {op} {k}; }}"
    if kind == "leaf2":
        op = r.choice(["+", "-", "*"])
        return f"function {name}({t} a, {t} b) {{ return a {op} b {op} {k}; }}"
    if kind == "condret":
        return f"function {name}({t} x) {{ if (x {cmp} {thr}) {{ return {k}; }} return x; }}"
    if kind == "condret_compute":
        return f"function {name}({t} x) {{ if (x {cmp} {thr}) {{ return x + {k}; }} return x - {one}; }}"
    if kind == "condret2":
        return f"function {name}({t} a, {t} b) {{ if (a {cmp} b) {{ return a; }} return b; }}"
    if kind == "condassign":
        return f"function {name}({t} x) {{ if (x {cmp} {thr}) {{ x = {k}; }} return x; }}"
    if kind == "condassign_compute":
        return f"function {name}({t} x) {{ if (x {cmp} {thr}) {{ x = x - {k}; }} return x; }}"
    if kind == "condassign_post":
        return f"function {name}({t} x) {{ if (x {cmp} {thr}) {{ x = {z}; }} {t} y = x * {one} + {one}; return y; }}"
    if kind == "abort_intradep":     # 0-x style: must abort cleanly
        return f"function {name}({t} x) {{ if (x {cmp} {thr}) {{ x = {z} - x; }} return x; }}"
    if kind == "abort_2branch":      # sign: two branches, must fall back
        return f"function {name}({t} x) {{ if (x > {z}) {{ return {one}; }} if (x < {z}) {{ return {z} - {one}; }} return {z}; }}"
    if kind == "abort_loop":         # loop in callee: must fall back
        return f"function {name}({t} x) {{ {t} acc = {z}; for (int j = 0, 3) {{ acc = acc + x; }} return acc; }}"
    raise ValueError(kind)

KINDS = ["leaf", "leaf2", "condret", "condret_compute", "condret2", "condassign",
         "condassign_compute", "condassign_post", "abort_intradep", "abort_2branch",
         "abort_loop"]

def arg_expr(r, ftype, idx):
    """An argument expression in terms of loop var i, of the given type."""
    md = r.choice([7, 10, 20, 50])
    off = r.randint(0, md)
    if ftype == "float":
        return f"(i % {md} - {off}) * 1.0"
    return f"(i % {md} - {off})"

def gen_program(r):
    n = r.choice([30000, 80000])
    ftype = r.choice(["int", "int", "float"])  # bias to int
    nh = r.randint(1, 3)
    kinds = [r.choice(KINDS) for _ in range(nh)]
    names = [f"h{j}" for j in range(nh)]
    two_arg = {"leaf2", "condret2"}
    defs = "\n".join(helper(r, names[j], kinds[j], ftype) for j in range(nh))

    acc_t = ftype
    az = "0.0" if ftype == "float" else "0"
    lines = [f"{acc_t} s = {az};"]
    # optional loop-carried extra
    carried = r.random() < 0.4
    if carried:
        lines.append(f"{acc_t} m = {az};")

    body = []
    # build a few call sites
    ncalls = r.randint(1, 3)
    for c in range(ncalls):
        j = r.randrange(nh)
        nm = names[j]
        if kinds[j] in two_arg:
            a1, a2 = arg_expr(r, ftype, 0), arg_expr(r, ftype, 1)
            call = f"{nm}({a1}, {a2})"
        else:
            call = f"{nm}({arg_expr(r, ftype, 0)})"
        # sometimes nest a second call as the arg (different helper -> should abort cleanly)
        if r.random() < 0.25 and nh > 1 and kinds[j] not in two_arg:
            j2 = (j + 1) % nh
            if kinds[j2] not in two_arg:
                call = f"{nm}({names[j2]}({arg_expr(r, ftype, 0)}))"
        # sometimes use the result in a caller-side branch
        if r.random() < 0.35:
            body.append(f"{acc_t} v{c} = {call};")
            thr = "5.0" if ftype == "float" else "5"
            if carried and r.random() < 0.5:
                body.append(f"if (v{c} {r.choice(['>','<'])} {thr}) {{ m = m + v{c}; }} else {{ m = m - v{c}; }}")
            else:
                body.append(f"if (v{c} {r.choice(['>','<'])} {thr}) {{ s = s + v{c}; }}")
        else:
            body.append(f"s = s + {call};")

    loop = f"for (int i = 0, {n}) {{ " + " ".join(body) + " }"
    lines.append(loop)
    tail = "s + m" if carried else "s"
    if ftype == "float":
        lines.append(f"print(({tail}) % 1000000.0);")
    else:
        lines.append(f"print(({tail}) % 1000000000);")
    return defs + "\n" + "\n".join(lines) + "\n"

def run(src, hot):
    open("/tmp/hz.spt", "w").write(src)
    env = dict(os.environ, SPT_JIT=("on" if hot else "0"))
    if hot:
        env["SPT_JIT_HOT"] = str(hot)
    try:
        return subprocess.run(["./build/bin/sptscript", "/tmp/hz.spt"], env=env,
                              capture_output=True, text=True, timeout=15).stdout.strip()
    except Exception as e:
        return "EXC/" + str(e)[:20]

def main():
    seed = int(sys.argv[sys.argv.index("--seed")+1]) if "--seed" in sys.argv else 1
    count = int(sys.argv[sys.argv.index("--count")+1]) if "--count" in sys.argv else 200
    r = random.Random(seed)
    match = mismatch = 0
    for c in range(count):
        src = gen_program(r)
        ic = run(src, 0)
        bad = False
        for hot in [16, 24, 35, 50]:
            jc = run(src, hot)
            if ic != jc:
                bad = True
                print(f"=== MISMATCH seed={seed} case={c} HOT={hot} ===")
                print(src)
                print(f"interp=[{ic}] jit=[{jc}]")
                break
        if bad: mismatch += 1
        else: match += 1
    print(f"seed {seed}: {match} match, {mismatch} MISMATCH")
    return 1 if mismatch else 0

if __name__ == "__main__":
    sys.exit(main())

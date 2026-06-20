#!/usr/bin/env python3
"""Differential fuzzer for float if-conversion (bit-exact bitmask blend).

Generates root-frame float branches of the shapes that now if-convert: clamp,
max/min-into, select-const, two-slot conditional update, with the full range of
comparisons (< <= > >= == !=), if-only and if-else, and FRACTIONAL operands (via
/7.0, /3.0) so that any rounding error in the blend would diverge from the
interpreter. Compares JIT-off vs JIT-on byte-for-byte across several HOT
thresholds. Any mismatch is a real bug (the blend must reproduce the chosen
double exactly).
"""
import subprocess, os, sys, random

CMP = ["<", "<=", ">", ">=", "==", "!="]
DIV = ["7.0", "3.0", "11.0"]

def fexpr(r, span):
    """A fractional float expression in terms of loop var i."""
    md = r.choice([50, 97, 173, 200])
    off = r.randint(0, md // 2)
    d = r.choice(DIV)
    return f"(i % {md} - {off}) / {d}"

def gen(r):
    n = r.choice([30000, 80000])
    # Case 3: integer condition (i%K==0, j<thresh) with float arms -- routed
    # through ICMPMASK. Mixed in with the float-condition (Case 2) shapes.
    if r.random() < 0.4:
        return gen_intcond(r, n)
    shape = r.choice(["clamp", "into", "select", "twoslot", "ifelse_assign"])
    cmp = r.choice(CMP)
    thr = f"{r.randint(-10,20)}.0"
    k1 = f"{r.randint(0,9)}.{r.randint(0,9)}"
    k2 = f"{r.randint(0,9)}.{r.randint(0,9)}"
    lines = []
    if shape == "clamp":
        lines = [f"float s = 0.0;",
                 f"for (int i = 0, {n}) {{ float v = {fexpr(r,1)}; if (v {cmp} {thr}) {{ v = {k1}; }} s = s + v; }}",
                 f"print(s);"]
    elif shape == "into":
        init = "0.0" if cmp in (">", ">=") else "999.0"
        lines = [f"float m = {init};",
                 f"for (int i = 0, {n}) {{ float v = {fexpr(r,1)}; if (v {cmp} m) {{ m = v; }} }}",
                 f"print(m);"]
    elif shape == "select":
        lines = [f"float s = 0.0;",
                 f"for (int i = 0, {n}) {{ float v = {fexpr(r,1)}; float w = 0.0; if (v {cmp} {thr}) {{ w = v; }} else {{ w = {k1}; }} s = s + w; }}",
                 f"print(s);"]
    elif shape == "twoslot":
        lines = [f"float a = 0.0; float b = 0.0;",
                 f"for (int i = 0, {n}) {{ float v = {fexpr(r,1)}; if (v {cmp} {thr}) {{ a = a + v; b = b + {k1}; }} }}",
                 f"print(a * 100000.0 + b);"]
    else:  # ifelse_assign: both arms assign the same slot different exprs
        lines = [f"float s = 0.0;",
                 f"for (int i = 0, {n}) {{ float v = {fexpr(r,1)}; float w = 0.0; if (v {cmp} {thr}) {{ w = v + {k1}; }} else {{ w = v - {k2}; }} s = s + w; }}",
                 f"print(s);"]
    return "\n".join(lines) + "\n"

def gen_intcond(r, n):
    """Case 3: integer condition + float arms (ICMPMASK)."""
    k1 = f"{r.randint(0,9)}.{r.randint(0,9)}"
    md = r.choice([2, 3, 4, 5])
    # condition forms on integers
    cform = r.choice([f"i % {md} == 0", f"i % {md} != 0",
                      f"(i % 200) > {r.randint(50,150)}",
                      f"(i % 200 - 100) < {r.randint(-50,50)}",
                      f"(i % 100) >= {r.randint(20,80)}"])
    shape = r.choice(["accum", "ifelse", "select", "twoslot"])
    if shape == "accum":
        body = f"if ({cform}) {{ s = s + v; }}"
        return f"float s = 0.0;\nfor (int i = 0, {n}) {{ float v = {fexpr(r,1)}; {body} }}\nprint(s);\n"
    if shape == "ifelse":
        body = f"if ({cform}) {{ s = s + v; }} else {{ s = s + {k1}; }}"
        return f"float s = 0.0;\nfor (int i = 0, {n}) {{ float v = {fexpr(r,1)}; {body} }}\nprint(s);\n"
    if shape == "select":
        body = f"float w = 0.0; if ({cform}) {{ w = v; }} else {{ w = {k1}; }} s = s + w;"
        return f"float s = 0.0;\nfor (int i = 0, {n}) {{ float v = {fexpr(r,1)}; {body} }}\nprint(s);\n"
    body = f"if ({cform}) {{ a = a + v; b = b + {k1}; }}"
    return f"float a = 0.0; float b = 0.0;\nfor (int i = 0, {n}) {{ float v = {fexpr(r,1)}; {body} }}\nprint(a * 100000.0 + b);\n"

def run(src, hot):
    open("/tmp/ffz.spt","w").write(src)
    e = dict(os.environ, SPT_JIT=("on" if hot else "0"))
    if hot: e["SPT_JIT_HOT"] = str(hot)
    try:
        return subprocess.run(["./build/bin/sptscript","/tmp/ffz.spt"],env=e,
                              capture_output=True,text=True,timeout=15).stdout.strip()
    except Exception as ex:
        return "EXC/"+str(ex)[:20]

def main():
    seed = int(sys.argv[sys.argv.index("--seed")+1]) if "--seed" in sys.argv else 1
    count = int(sys.argv[sys.argv.index("--count")+1]) if "--count" in sys.argv else 150
    r = random.Random(seed)
    m = mm = 0
    for c in range(count):
        src = gen(r); ic = run(src, 0); bad = False
        for hot in [16, 24, 40]:
            jc = run(src, hot)
            if ic != jc:
                bad = True
                print(f"=== MISMATCH seed={seed} case={c} HOT={hot} ===\n{src}interp=[{ic}] jit=[{jc}]")
                break
        if bad: mm += 1
        else: m += 1
    print(f"seed {seed}: {m} match, {mm} MISMATCH")
    return 1 if mm else 0

if __name__ == "__main__":
    sys.exit(main())

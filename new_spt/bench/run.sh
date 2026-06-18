#!/usr/bin/env bash
# bench/run.sh — build the SPT drivers, run all three engines on every benchmark,
# and print a comparison table. Times are best-of-K process CPU seconds (lower is
# better). Run from the project root: bench/run.sh
set -e
cd "$(dirname "$0")/.."

SRC="src/mem.c src/object.c src/state.c src/vm.c src/api.c src/lexer.c src/parser.c src/codegen.c"
MIR_OBJ="build/mir.o build/mir-gen.o"

# MIR objects come from a JIT build; make them if missing.
if [ ! -f build/mir.o ] || [ ! -f build/mir-gen.o ]; then make JIT=1 >/dev/null 2>&1; fi

echo ">> building SPT benchmark drivers"
cc -std=gnu11 -O2 -Iinclude -Isrc $SRC bench/bench_spt.c -lm -o bench/bench_spt_interp
cc -std=gnu11 -O2 -DSPT_JIT -Iinclude -Isrc -Ithird_party/mir $SRC src/jit/jit_mir.c \
   bench/bench_spt.c $MIR_OBJ -lm -ldl -lpthread -o bench/bench_spt_jit

echo ">> running benchmarks (best-of-7 CPU seconds)"
./bench/bench_spt_interp > /tmp/bench_interp.tsv 2>/tmp/bench_interp.err || true
./bench/bench_spt_jit    > /tmp/bench_jit.tsv    2>/tmp/bench_jit.err    || true
lua5.4 bench/bench_lua.lua > /tmp/bench_lua.tsv  2>/tmp/bench_lua.err    || true
cat /tmp/bench_interp.err /tmp/bench_jit.err /tmp/bench_lua.err 1>&2 || true

awk -F'\t' '
  FNR==NR && FILENAME==i { it[$1]=$2; res_i[$1]=$3; order[++n]=$1; next }
  FILENAME==j            { jt[$1]=$2; res_j[$1]=$3; next }
  FILENAME==l            { lt[$1]=$2; res_l[$1]=$3 }
  END {
    printf "\n%-11s %10s %10s %10s   %9s %9s %9s\n",
           "benchmark","interp(s)","jit(s)","lua(s)","jit/intp","lua/jit","lua/intp";
    printf "%-11s %10s %10s %10s   %9s %9s %9s\n",
           "-----------","---------","---------","---------","---------","---------","---------";
    for (k=1;k<=n;k++){ b=order[k];
      ji = (jt[b]>0)? it[b]/jt[b] : 0;     # how many x JIT beats SPT-interp
      lj = (jt[b]>0)? lt[b]/jt[b] : 0;     # >1 => SPT-jit faster than Lua
      li = (it[b]>0)? lt[b]/it[b] : 0;     # >1 => SPT-interp faster than Lua
      printf "%-11s %10.4f %10.4f %10.4f   %8.2fx %8.2fx %8.2fx\n",
             b, it[b], jt[b], lt[b], ji, lj, li;
    }
    print "";
    print "Legend: jit/intp = SPT-JIT speedup over SPT-interpreter (higher better).";
    print "        lua/jit  > 1 means SPT-JIT is faster than PUC-Lua; < 1 means slower.";
    print "        lua/intp > 1 means SPT-interpreter is faster than PUC-Lua; < 1 means slower.";
  }
' i=/tmp/bench_interp.tsv j=/tmp/bench_jit.tsv l=/tmp/bench_lua.tsv \
  /tmp/bench_interp.tsv /tmp/bench_jit.tsv /tmp/bench_lua.tsv

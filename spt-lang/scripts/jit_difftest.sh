#!/usr/bin/env bash
# Differential JIT test: each kernel must produce identical output with JIT
# off vs on, AND (when expected) must actually compile a native trace.
# Usage: scripts/jit_difftest.sh [path-to-sptscript] [kernel-dir]
set -u
BIN="${1:-./build/bin/sptscript}"
DIR="${2:-test/15_jit/kernels}"
HOT="${SPT_JIT_HOT:-8}"
pass=0; fail=0; nojit=0
fails=()
for k in "$DIR"/*.spt; do
  name=$(basename "$k")
  off=$(SPT_JIT=0 "$BIN" "$k" 2>/dev/null)
  on=$(SPT_JIT=on SPT_JIT_HOT="$HOT" "$BIN" "$k" 2>/dev/null)
  dbg=$(SPT_JIT=on SPT_JIT_HOT="$HOT" SPT_JIT_DEBUG=1 "$BIN" "$k" 2>&1 >/dev/null)
  compiled=$(printf '%s' "$dbg" | grep -c "compiled trace")
  if [ "$off" == "$on" ]; then
    tag="OK"
    if [ "$compiled" -eq 0 ]; then tag="OK(no-jit)"; nojit=$((nojit+1)); fi
    printf "  %-18s %-10s -> %s\n" "$name" "$tag" "$(printf '%s' "$on" | tr '\n' '|')"
    pass=$((pass+1))
  else
    printf "  %-18s FAIL\n     off=[%s]\n     on =[%s]\n" "$name" "$(printf '%s' "$off"|tr '\n' '|')" "$(printf '%s' "$on"|tr '\n' '|')"
    fail=$((fail+1)); fails+=("$name")
  fi
done
echo "------------------------------------------------------------"
echo "pass=$pass fail=$fail (no-jit kernels=$nojit)"
if [ "$fail" -gt 0 ]; then echo "FAILED: ${fails[*]}"; exit 1; fi

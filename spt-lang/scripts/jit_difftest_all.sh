#!/usr/bin/env bash
# Full differential JIT test: every test/**/*.spt must produce identical output
# with JIT off vs on. This is the broad safety net (the kernels in
# jit_difftest.sh are the focused, compile-checked subset).
#
# Usage: scripts/jit_difftest_all.sh [path-to-sptscript]
#
# Known benign mismatch: test/vm_bug_verify/b4_yield_return.spt prints a
# nondeterministic heap address even with JIT off, so it is skipped.
set -u
BIN="${1:-./build/bin/sptscript}"
HOT="${SPT_JIT_HOT:-40}"
SKIP_RE='b4_yield_return'

match=0; mismatch=0; timeout_n=0
fails=()
while IFS= read -r f; do
  case "$f" in *$SKIP_RE*) continue;; esac
  off=$(SPT_JIT=0 "$BIN" "$f" 2>/dev/null)
  on=$(timeout 30 env SPT_JIT=on SPT_JIT_HOT="$HOT" "$BIN" "$f" 2>/dev/null)
  rc=$?
  if [ "$rc" -eq 124 ]; then
    printf "  TIMEOUT  %s\n" "$f"; timeout_n=$((timeout_n+1)); fails+=("$f"); continue
  fi
  if [ "$off" == "$on" ]; then
    match=$((match+1))
  else
    printf "  DIFF     %s\n     off=[%s]\n     on =[%s]\n" "$f" \
      "$(printf '%s' "$off" | tr '\n' '|' | cut -c1-120)" \
      "$(printf '%s' "$on"  | tr '\n' '|' | cut -c1-120)"
    mismatch=$((mismatch+1)); fails+=("$f")
  fi
done < <(find test -name '*.spt' | sort)

echo "------------------------------------------------------------"
echo "match=$match mismatch=$mismatch timeout=$timeout_n"
if [ "$mismatch" -gt 0 ] || [ "$timeout_n" -gt 0 ]; then
  echo "FAILED: ${fails[*]}"; exit 1
fi

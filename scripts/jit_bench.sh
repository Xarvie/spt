#!/bin/bash
# jit_bench.sh — benchmark the SPT trace JIT against the interpreter.
# Runs several loop shapes, reports interpreter vs JIT wall time + speedup,
# and verifies the two produce identical output.
#
# Usage: scripts/jit_bench.sh [path-to-sptscript]
set -u
BIN="${1:-./build/bin/sptscript}"
HOT="${SPT_JIT_HOT:-50}"
REPS="${REPS:-4}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# name|source
cat > "$TMP/int_for.spt"   <<'EOF'
int s = 0;
for (int r = 0, 200) { for (int i = 0, 1000000) { s = s + i; } }
print(s);
EOF
cat > "$TMP/int_while.spt" <<'EOF'
int s = 0; int i = 0;
while (i < 200000000) { s = s + i; i = i + 1; }
print(s);
EOF
cat > "$TMP/int_muldiv.spt" <<'EOF'
int s = 0;
for (int r = 0, 200) { for (int i = 1, 1000000) { s = s + (i * 3) / 2 - (i % 7); } }
print(s);
EOF
cat > "$TMP/float_acc.spt" <<'EOF'
float s = 0.0;
for (int r = 0, 200) { for (int i = 0, 1000000) { s = s + 1.5; } }
print(s);
EOF
cat > "$TMP/float_mix.spt" <<'EOF'
float s = 1.0;
for (int r = 0, 200) { for (int i = 1, 1000000) { s = s * 1.0000001 + 0.5; } }
print(s);
EOF
cat > "$TMP/branch.spt" <<'EOF'
int s = 0;
for (int r = 0, 200) { for (int i = 0, 1000000) { if (i % 2 == 0) { s = s + i; } else { s = s - 1; } } }
print(s);
EOF

timeit() { # $1=env-assignment, $2=file ; echo best-of-REPS seconds
  local best=""
  for _ in $(seq "$REPS"); do
    local t0 t1
    t0=$(date +%s.%N)
    env $1 "$BIN" "$2" >/dev/null 2>&1
    t1=$(date +%s.%N)
    local d; d=$(awk "BEGIN{print $t1-$t0}")
    if [ -z "$best" ] || awk "BEGIN{exit !($d < $best)}"; then best=$d; fi
  done
  echo "$best"
}

printf "%-12s %12s %12s %9s   %s\n" "bench" "interp(s)" "jit(s)" "speedup" "correct"
printf -- "------------------------------------------------------------------\n"
for f in int_for int_while int_muldiv float_acc float_mix branch; do
  src="$TMP/$f.spt"
  oi=$(env SPT_JIT=0 "$BIN" "$src" 2>/dev/null)
  oj=$(env SPT_JIT=on SPT_JIT_HOT="$HOT" "$BIN" "$src" 2>/dev/null)
  ti=$(timeit "SPT_JIT=0" "$src")
  tj=$(timeit "SPT_JIT=on SPT_JIT_HOT=$HOT" "$src")
  spd=$(awk "BEGIN{printf \"%.1fx\", $ti/$tj}")
  ok=$([ "$oi" = "$oj" ] && echo "yes" || echo "NO <<<")
  printf "%-12s %12.3f %12.3f %9s   %s\n" "$f" "$ti" "$tj" "$spd" "$ok"
done

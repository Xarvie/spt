#!/usr/bin/env bash
# spt-lsp/server TDD 循环：编译 (服务器核心 + 复用的前端) + 单元测试 + 端到端冒烟。
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

FRONTEND_DIR="../../src/frontend"
INC="-Ithird_party/cjson -Isrc/rpc -Isrc/lsp -Isrc/analysis -Isrc/features -I$FRONTEND_DIR"
CFLAGS="-std=c11 -g -O0 -Wall -Wextra"
OBJ=/tmp/lspobj
mkdir -p "$OBJ"
fail=0

FRONTEND_SRCS="spt_arena.c spt_ast.c spt_diag.c spt_lexer.c spt_parser.c spt_lsp_bridge.c"
CORE_SRCS="src/rpc/spt_rpc.c src/lsp/server.c src/lsp/trace.c src/lsp/protocol.c src/lsp/documents.c \
           src/analysis/semantic.c src/analysis/workspace.c \
           src/features/diagnostics.c src/features/symbols.c src/features/semantic_tokens.c \
           src/features/hover.c src/features/definition.c src/features/completion.c \
           src/features/references.c src/features/rename.c src/features/signature.c src/features/format.c"

compile() {
  local src="$1" o
  o="$OBJ/$(echo "$src" | tr '/.' '__').o"
  if [ ! -f "$o" ] || [ "$src" -nt "$o" ]; then
    local cf="$CFLAGS"
    case "$src" in */cJSON.c) cf="-std=c11 -O2 -w";; esac
    gcc $cf $INC -c "$src" -o "$o" 2>/tmp/cc1.log || { echo "[CC FAIL] $src"; cat /tmp/cc1.log >&2; return 1; }
  fi
  echo "$o"
}

echo "== compile objects ==" >&2
OBJS=""
for s in third_party/cjson/cJSON.c; do o=$(compile "$s") || fail=1; OBJS="$OBJS $o"; done
for s in $FRONTEND_SRCS; do
  [ -f "$FRONTEND_DIR/$s" ] || { echo "missing frontend $s" >&2; continue; }
  o=$(compile "$FRONTEND_DIR/$s") || fail=1; OBJS="$OBJS $o"
done
for s in $CORE_SRCS; do
  [ -f "$s" ] || continue
  o=$(compile "$s") || fail=1; OBJS="$OBJS $o"
done
[ $fail -eq 1 ] && { echo "BUILD FAILED"; exit 1; }

echo "== build sptlsp ==" >&2
o=$(compile src/main.c) || { echo main fail; exit 1; }
gcc $OBJS "$o" -lm -o /tmp/sptlsp 2>/tmp/ln.log || { echo "LINK FAIL"; cat /tmp/ln.log; exit 1; }
echo "   -> /tmp/sptlsp"

echo "== unit tests =="
for t in test/*.c; do
  [ -f "$t" ] || continue
  name=$(basename "$t" .c)
  to=$(compile "$t") || { fail=1; continue; }
  gcc $OBJS "$to" -lm -o /tmp/$name 2>/tmp/tln.log || { echo "[LINK FAIL] $t"; cat /tmp/tln.log; fail=1; continue; }
  if /tmp/$name >/tmp/$name.out 2>&1; then echo "  PASS $name"; else echo "  FAIL $name"; cat /tmp/$name.out; fail=1; fi
done

echo "== end-to-end pipe smoke =="
frame() { local b="$1"; printf 'Content-Length: %d\r\n\r\n%s' "${#b}" "$b"; }
out=$( {
  frame '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}'
  frame '{"jsonrpc":"2.0","method":"initialized","params":{}}'
  frame '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t.spt","languageId":"sptscript","version":1,"text":"int x = ;\n"}}}'
  frame '{"jsonrpc":"2.0","id":2,"method":"shutdown"}'
  frame '{"jsonrpc":"2.0","method":"exit"}'
} | /tmp/sptlsp )
rc=$?
ok=1
echo "$out" | grep -q '"capabilities"' || { echo "  smoke FAIL: no capabilities"; ok=0; }
echo "$out" | grep -q '"id":2'         || { echo "  smoke FAIL: no shutdown response"; ok=0; }
[ $rc -eq 0 ]                           || { echo "  smoke FAIL: exit $rc"; ok=0; }
if echo "$out" | grep -q 'publishDiagnostics'; then echo "  (diagnostics wired: publishDiagnostics observed)"; fi
[ $ok -eq 1 ] && echo "  PASS smoke" || fail=1

if [ $fail -eq 0 ]; then echo "ALL GREEN"; else echo "FAILURES"; exit 1; fi

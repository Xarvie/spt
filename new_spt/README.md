# SPT

SPT is a from-scratch implementation of a Lua 5.5–class dynamic language with
C++-flavoured syntax. It pairs a fast register VM with an **optional** MIR-backed
JIT that can be toggled at build time and is disabled automatically on platforms
that forbid runtime code generation (notably iOS). The interpreter is always
present and authoritative; the JIT is a purely additive fast path.

This repository contains the **engine core, a working source frontend, and a
gradual static type system**. It compiles cleanly with `-Wall -Wextra`, runs
clean under AddressSanitizer + UndefinedBehaviorSanitizer with leak detection,
and ships two demos: one that exercises every engine subsystem from hand-written
bytecode, and one that compiles and runs real SPT **source text** — including
type-checked, typed code that compiles through to native via the JIT. A few
language features remain to be built out; see the Roadmap.

## What runs today

Engine subsystems, driven from bytecode (`spt_demo`):

```
== SPT core demo ==

1. typed int   (2+3)*4         = 20
2. list        [10,20,30][1]   = 20   (length 3, 0-based)
3. map         m["hp"]          = 100
4. C interop   add(7, 35)      = 42
   global read _G.answer       = 42
5. closure     addfn(100, 23)  = 123
6. gc          live objects: 19 -> 2019 (peak) -> 4 (after collect)
7. jit         factorial(12): interp=479001600  native=479001600  [compiled to native, results match]
```

Real SPT source, compiled and run (`spt_run`):

```
function fib(n) {                 function sum(n) {
  if (n < 2) { return n; }          s = 0; i = 1;
  return fib(n-1) + fib(n-2);       while (i <= n) { s = s + i; i = i + 1; }
}                                   return s;
print(fib(20));   // 6765         }
                                  print(sum(100));  // 5050
xs = [10, 20, 30, 40];
xs[1] = 99;                       function classify(n) {
print(xs[0]+xs[1]+xs[3]);// 149     if (n % 2 == 0) { return "even"; }
                                    return "odd";
print((2+3)*4 - 10/2);  // 15     }
```

A register interpreter with direct-threaded dispatch; a complete value/object
model with a generational-GC-ready heap; the List/Map container split; the
Slot-0 receiver calling convention; C interoperation; a working mark/sweep
collector; a **working MIR JIT** that lowers hot prototypes to native code
(verified to match the interpreter bit-for-bit on line 7); and a **source
frontend** — lexer, recursive-descent parser, a gradual static type system, and
bytecode codegen — that turns the programs above (recursion, loops, lists,
typed code, and **closures with upvalue capture**) into runnable bytecode. The
JIT now lowers calls and closures as well, so whole recursive functions and
closure factories — not just straight-line typed arithmetic — compile through
to native code.

## Architecture

The engine is layered so the JIT can be removed without touching the rest:

```
  frontend (source text → tokens → AST)
    lexer.c     tokenizer
    parser.c    recursive-descent parser → AST (arena-allocated)
    compiler.h  shared tokens / AST / stage interface
  codegen  (AST → bytecode Proto)
    codegen.c   register-stack codegen, jump patching, spt_load()
  ────────────────────────────────────────────────────────
  core runtime (always built, authoritative)
    value model     value.h        16-byte tagged union TValue
    object model    object.h       String / Table(List|Map) / Proto / Closure
    bytecode ISA    opcodes.h      byte-aligned 32-bit, typed + List fast ops
    interpreter     vm.c           computed-goto dispatch, Slot-0 calls
    memory + GC     mem.c          accounted allocator, mark/sweep collector
    C API           api.c          stack-based embedding interface
  ────────────────────────────────────────────────────────
  JIT layer (optional, compile-time gated by SPT_JIT)
    jit/jit.h       interface; collapses to no-ops when disabled
    jit/jit_mir.c   MIR backend: bytecode -> MIR IR -> native code
    jit/jit_rt.h    runtime helpers the generated code calls into
```

When `SPT_JIT` is off, `jit/jit.h` turns every JIT entry point into an inline
no-op and no MIR object code is compiled or linked. The same sources build both
configurations unchanged — interpreter-only on iOS, JIT-accelerated elsewhere.

### How the JIT lowers code

For a hot prototype the backend emits a MIR function `int spt_fn_N(spt_State *L)`
that loads the frame base once and translates each bytecode. Type-stable, hot
operations become straight native code with **no calls and no runtime tag
dispatch** — e.g. `OP_IADD` lowers to two loads, a native `ADD`, and two stores.
Operations whose semantics must match the interpreter across all value types
(comparison, truthiness, the return sequence) are emitted as calls to the small
helpers in `jit_rt.h`. The backend pre-scans each prototype and, if it contains
an opcode the lowering does not yet cover, it **bails** — `Proto::jit_entry`
stays NULL and the always-correct interpreter runs that prototype. Coverage thus
grows safely, one opcode at a time. (The demo compiles an iterative factorial and
checks the native result against the interpreter.)

### Design decisions worth knowing

- **Value representation.** A 16-byte tagged union (8-byte payload + tag), not
  NaN-boxing. This keeps values trivial to pass across the C boundary and easy to
  debug; NaN-boxing can be slotted behind the same interface later if the 8-byte
  saving is ever worth it.
- **List vs Map split.** Lua's unified table is split into a `List` (0-based,
  dense, bounds-checked, `#` is its length) and a `Map` (arbitrary keys). A
  value's mode is fixed at creation. This makes list indexing a single bounds
  check plus a load — no array/hash dispatch, no metatable probe on the hot path.
- **Slot-0 receiver convention.** Every call reserves register 0 of the callee
  frame for the receiver; named arguments occupy registers 1..N. Plain calls pass
  a null receiver. This is what lets method-call and `__call` semantics share one
  uniform frame layout.
- **Typed opcodes.** When the (future) codegen proves both operands are `int` or
  `float`, it emits typed arithmetic (`OP_IADD`, `OP_FADD`, …) that skips all
  runtime tag dispatch. Together with the List ops, this is how the interpreter is
  designed to beat stock Lua without relying on the JIT.
- **Dispatch.** Direct-threaded (computed goto) on GCC/Clang — the same technique
  PUC-Lua uses — with an automatic switch fallback on toolchains without
  labels-as-values (e.g. MSVC).
- **Garbage collection.** The implemented collector is a correct stop-the-world
  tri-colour mark/sweep that runs only at instruction boundaries. Object headers
  already carry the colour + age bits a generational collector needs, and every
  mutation site already calls the write barrier, so the generational upgrade is
  additive rather than a rewrite.

## The language today

`spt_load(L, source, name)` compiles SPT source text to bytecode and leaves a
callable closure on the stack; `spt_run` (examples/run_source.c) shows the full
round trip. The implemented subset:

- **Variables** are local by default. `x = e;` declares or updates a local;
  `global x = e;` writes a global. A bare name reads a local if one is in scope,
  otherwise a global.
- **Static types (opt-in).** Annotate a declaration or parameter with `int`,
  `float`, `string`, or `bool` and the type is checked at compile time and used
  to emit faster code. `const` makes a binding immutable. Typing is gradual:
  unannotated code is fully dynamic and behaves exactly as before. Literals carry
  their own type, so even unannotated constant arithmetic is typed.
  ```
  int n = 10;                  // typed local
  const float PI = 3.14159;    // immutable
  function area(float r) -> float { return PI * r * r; }
  int x = "hello";             // compile error: cannot initialize int with string
  ```
  When both operands of `+ - * %` are statically the same numeric type, the
  codegen emits the **typed opcodes** (`OP_IADD`, `OP_IMUL`, …) that skip all
  runtime tag dispatch — and the typed *integer* forms are exactly what the JIT
  lowers, so a typed integer function compiles straight to native code (the
  `spt_run` demo compiles a typed `fact` and checks the native result against the
  interpreter). The type system is intentionally sound: there are no implicit
  conversions yet, so a value crosses between dynamic and typed only where the
  static types already agree (explicit casts are a later step).
- **Expressions**: `+ - * / %`, comparisons `== != < <= > >=`, unary `-` and `!`,
  parentheses, calls `f(a, b)`, indexing `a[i]`, integer/float/string/`true`/
  `false`/`null` literals, and List literals `[a, b, c]`.
- **Control flow**: `if (c) { … } else { … }`, `while (c) { … }`, blocks, and
  `return e;` / `return;`.
- **Functions and closures**: `function f(params) -> ret { … }` compiles to its
  own Proto. A top-level function binds a global, so it is callable everywhere,
  **including recursively**. A function nested inside another is a **closure**: it
  captures the enclosing function's locals as upvalues, can mutate them, and can
  be returned to outlive the scope that created it — each instance captures
  independently.
  ```
  function adder(int n) {
    function add(int x) { return x + n; }   // captures n
    return add;
  }
  add5 = adder(5);   print(add5(10));   // 15
  ```
  Parameter and return types are optional; a declared return type lets callers
  infer the result type. Calls follow the Slot-0 convention automatically.

Comments are `//` line and `/* … */` block. Not yet in the frontend: explicit
casts at the dynamic/typed boundary, Map literals, `&&` / `||`, and block-scoped
locals.

## Building

### Desktop / server (JIT enabled, the default)

```sh
cmake -S . -B build -DSPT_JIT=ON
cmake --build build -j
ctest --test-dir build
./build/spt_demo
```

### Interpreter-only (the iOS configuration)

```sh
cmake -S . -B build -DSPT_JIT=OFF
cmake --build build -j
./build/spt_demo
```

On an actual iOS target (`-DCMAKE_SYSTEM_NAME=iOS`) the JIT is forced off
regardless of `-DSPT_JIT`, and no MIR code is linked — the build is App Store
compliant by construction.

### Quick Makefile

```sh
make           # interpreter-only
make JIT=1     # JIT build (links the vendored MIR)
```

### Notes

- The JIT links a vendored copy of MIR under `third_party/mir`. Point the build
  at a different copy with `-DSPT_MIR_DIR=<path>`.
- The interpreter-only binary is tiny (tens of KB); linking MIR for the JIT adds
  the code generator. That size gap is the separable-JIT design paying off on
  constrained targets.

## Project layout

```
include/spt/   public headers (conf, value, object, opcodes, state, mem)
include/spt.h  umbrella embedding API (incl. spt_load and JIT control)
src/           engine: mem, object, state, vm, api
src/           frontend: lexer.c, parser.c, codegen.c, compiler.h
src/jit/       JIT interface + MIR backend (built only when SPT_JIT)
examples/      demo.c — bytecode subsystem tour
               run_source.c — compile-and-run real SPT source
third_party/   vendored MIR (JIT backend)
CMakeLists.txt primary build      Makefile  quick build
```

## Roadmap

1. **JIT coverage — essentially complete.** The lowering handles moves,
   constant/immediate loads, typed integer *and* float arithmetic, generic
   (dynamically typed) arithmetic, value comparisons, conditional and
   unconditional branches, `return`, global access, upvalue get/set, List/Map
   index access, data-structure construction, length, concatenation, casts, and
   unary negation. As of Stage 4 it also lowers **`OP_CALL` and `OP_CLOSURE`** —
   so a compiled function calls other functions (itself included) and builds
   closures without leaving native code. Because `do_call` dispatches on the
   callee's `jit_entry`, recursion runs fully native once compiled; and because
   a nested call can reallocate the value stack, the generated code reloads its
   cached frame base after every call (deep native recursion that grows the
   stack stays correct — see the `deep(400)` differential test). Threshold
   auto-compilation is wired (`SPT_JIT_THRESHOLD`); the demos force-compile to
   make the path explicit. Remaining: the multi-result/stack-top call form
   (`B==0`/`C==0`, which still bails to the interpreter), and a guard/deopt path
   so a prototype can be speculatively compiled and fall back on a cold edge.
2. **Generational GC.** Partition the object list into young/old generations and
   make the (already-present) write barrier record old→young edges. PUC-Lua's
   non-moving generational collector is the blueprint.
3. **Frontend: casts and more sugar.** Lexer, parser, codegen, a gradual static
   **type system** (`int`/`float`/`string`/`bool`, `const`, compile-time checking,
   typed-opcode emission), and **closures** with upvalue capture are all in place.
   Remaining: explicit casts at the dynamic/typed boundary (to move values soundly
   between dynamic and typed code and to type `declare`d externs), Map literals,
   `&&` / `||` short-circuit, and block-scoped locals.
4. **Interpreter polish.** Non-recursive (soft) `OP_CALL` to remove C-stack depth
   limits on SPT recursion, Map deletion with tombstones, string indexing in the
   generic index path, and metatable dispatch.

## License

The MIR JIT backend under `third_party/mir` is MIT-licensed (see
`third_party/mir/LICENSE`). A license for SPT itself is to be determined by the
project owner.

# mylang — Bytecode VM Notes

Status: in progress. This documents the VM's internal design, separate
from `language-spec.md` (which documents the LANGUAGE — the VM is
purely an alternate execution engine for the same language, invisible
to anyone writing `.mylang` scripts once it's complete).

## Why a second execution engine

The tree-walking interpreter (`src/interpreter/`) works by directly
walking the AST every time a script runs — each node dispatches through
a virtual function call, and the same node gets re-interpreted from
scratch every time it executes (e.g. every loop iteration). This is
simple to build and reason about, which is why it came first, but it's
slower than it could be: virtual dispatch overhead, poor cache locality
from a pointer-heavy tree structure, and no reuse of work across repeated
executions of the same code.

A bytecode VM (`src/vm/`) fixes this by splitting execution into two
phases: **compile** (walk the source once, ahead of time, into a flat
array of simple instructions) and **run** (a tight loop that reads those
instructions and acts on them using a value stack — no tree, no
recursion into node objects, just array indexing and a switch
statement). This is how CPython, Lua, and the JVM all work internally.

## Current status

Implemented: number/string/boolean/nil literals, unary `-`/`!`, binary
`+ - * /` with correct precedence and left-associativity, comparisons
(`== != > >= < <=`), logical `and`/`or` (short-circuiting, returning an
actual operand value like the tree-walker — not necessarily a bool),
parenthesized grouping, `print`, global and local variables with real
lexical scoping, and now **control flow**: `if`/`else`, `while`, and
`for` (including omitted initializer/condition/increment clauses, and a
for-loop's own variable correctly scoped to the loop only).

Not yet implemented (in rough build order): functions and closures,
classes/inheritance, string concatenation via `+` (currently number-only
in the VM), arrays/maps, and a real garbage collector (deferred —
likely stays on `shared_ptr`/reference counting initially, same as the
tree-walker, until/unless GC becomes a specific goal of its own).

## Architecture

- **`opcode.h`** — the instruction set. One enum value per instruction;
  some are followed by an operand byte (e.g. `OP_CONSTANT`'s index into
  the constant pool).
- **`vm_value.h`** — the VM's own value type, deliberately SEPARATE from
  the tree-walker's `Value` (`src/interpreter/value.h`). Currently a
  small `std::variant<monostate, double, string>` (nil / number /
  string — strings now back both variable-name constants AND real
  string literals, e.g. `print "hello";`, though `+`-concatenation on
  them isn't wired up yet); will grow further as more types are added,
  rather than adopting the tree-walker's `Value` wholesale (which would
  drag in `Callable`/`LoxInstance`/etc. before the VM has any use for
  them).
- **`chunk.h`/`.cpp`** — one compiled unit: a flat byte array (`code`), a
  constant pool (`constants`), and a parallel line-number array
  (`lines`) for error reporting.
- **`compiler.h`/`.cpp`** — a single-pass Pratt parser: reads tokens
  (reusing the existing `Lexer`/`Token` from `src/lexer/`) and emits
  bytecode directly, with no separate AST step. Precedence is handled
  via a per-token-type table (`getRule()`) rather than the tree-walker's
  ladder of `equality()`/`comparison()`/`term()`/`factor()` functions —
  this is the standard technique for a bytecode compiler's front end
  (same approach as `clox` in *Crafting Interpreters*).
- **`vm.h`/`.cpp`** — the execution loop itself: reads one instruction at
  a time via an instruction pointer (`ip`) into `chunk.code`, acting on
  a `std::vector<VMValue>` stack.

## Design decision: separate compiler, not reusing the tree-walker's AST

The tree-walker's `ExprVisitor`/`StmtVisitor` interfaces now have
~15 pure virtual methods each (covering functions, classes, arrays,
maps, imports, ...). Building the VM's compiler as a partial
implementation of those interfaces would mean writing "not yet
supported" stub overrides for everything the VM doesn't handle yet —
significant boilerplate that provides no value until the VM actually
catches up feature-for-feature. A direct token-to-bytecode compiler
avoids this entirely and is free to grow at its own pace, one language
construct at a time, independent of how big the tree-walker's AST has
grown.

## Design note: how assignment targets are validated (`canAssign`)

Unlike the tree-walker's parser — which parses a full expression tree
first and only *afterward* checks whether the parsed node happens to be
a `Variable` (making it a valid assignment target) — the VM's Pratt
parser threads a `canAssign` boolean through every prefix/infix parse
function. `canAssign` is only true when `parsePrecedence` was entered at
`ASSIGNMENT` precedence or lower. This is what makes `a + b = c`
correctly fail to parse: by the time `b` is parsed (as the right operand
of `+`, at `TERM + 1` precedence), `canAssign` is false, so `variable()`
won't treat a trailing `=` as an assignment — it's simply a leftover
token, caught by `parsePrecedence`'s trailing "stray `=`" check and
reported as `Invalid assignment target.`

## Bug caught during testing: dangling reference from a temporary

The first version of `OP_DEFINE_GLOBAL`/`OP_GET_GLOBAL`/`OP_SET_GLOBAL`
wrote:
```cpp
const std::string& name = asVMString(readConstant());
```
`readConstant()` returns a `VMValue` **by value** — a temporary.
`asVMString()` returns a `const std::string&` *into* that temporary.
The temporary is destroyed at the end of the statement, leaving `name`
a dangling reference — undefined behavior that manifested as an
immediate segfault on the simplest possible test (`var x = 10;`). Fixed
by copying the string (`std::string name = asVMString(readConstant());`)
instead of binding a reference to it. Worth remembering as a general
pattern: never bind a `const&` to the result of a function that returns
a reference into a temporary you don't otherwise keep alive.

## Design note: how local variables are resolved (compile-time stack slots)

A local variable's "address" is simply its position in the `Compiler`'s
`locals` vector at compile time — which is engineered to always mirror
exactly what's sitting on the VM's runtime value stack at that point in
execution. So `OP_GET_LOCAL <slot>` / `OP_SET_LOCAL <slot>` are just a
direct array index (`stack[slot]`) at runtime — no name, no hash-map
lookup, unlike globals. This is the actual performance payoff of
"proper" local variables in a bytecode VM, and it's why the compiler
needs to track scope depth and a locals list at all: getting this
compile-time bookkeeping right is what makes the runtime access trivial.

Two subtle correctness cases handled deliberately:
- **`var a = a;`** — `declareVariable()` records the local with a
  sentinel depth of `-1` ("declared but not yet initialized") before
  its initializer expression is compiled. If that initializer tries to
  reference the same name, `resolveLocal()` sees the sentinel and
  reports a compile error, rather than silently reading whatever
  garbage happens to be in that stack slot.
- **Shadowing vs. duplicate declaration** — redeclaring the same name
  in the exact same block is a compile error (`declareVariable()` scans
  backward through `locals` but only within the current scope depth);
  redeclaring it in a *nested, deeper* block is normal shadowing and
  is allowed, matching how the tree-walker's `Environment` chaining
  already behaves.

## Design note: how jumps and backpatching work

Unlike everything compiled before this increment (which just emits bytes
in a straight line as parsing proceeds), a jump's DESTINATION often
isn't known until after its body has been compiled — e.g. an `if`
doesn't know how many bytes its then-branch will occupy until that
branch has actually been emitted. The standard fix (`emitJump`/
`patchJump` in `compiler.cpp`): emit the jump opcode with a 2-byte
PLACEHOLDER offset, remember that byte position, keep compiling, then
go back and overwrite the placeholder with the real, now-known distance
(`Chunk::patchJumpAt`). Backward jumps (`OP_LOOP`, used to return to a
loop's condition) don't need this trick — the loop's start position is
already known by the time the jump is emitted, so the distance is
computed immediately.

## Bug caught during testing: OP_JUMP_IF_FALSE peek vs. pop

The first version of `OP_JUMP_IF_FALSE` popped the condition value
unconditionally before deciding whether to jump. This is wrong: `if`
and `while` both already emit their OWN explicit `OP_POP` for the
condition (once for the then-branch, once for the else-branch) —
if `OP_JUMP_IF_FALSE` also popped, that's a double-pop, silently
corrupting the stack. Worse, `and`/`or`'s short-circuit behavior
specifically depends on the falsey/truthy operand SURVIVING on the
stack as the expression's result when short-circuiting — popping it
inside the jump instruction would throw that value away instead of
returning it. Fixed by making `OP_JUMP_IF_FALSE` only PEEK the
condition; every caller (`if`, `while`, `for`, `and_`, `or_`) is
responsible for popping it explicitly wherever that's actually correct
for that construct — caught during code review before ever running it,
by tracing through what `if`/`and_`/`or_` each assumed about the
opcode's contract.

## How to run it

```bash
./mylang --vm script.mylang     # run a file through the VM
./mylang --vm                    # VM REPL
./mylang script.mylang           # tree-walking interpreter (full language, default)
```

The two engines are completely independent — `--vm` is currently a
small subset of the language; everything else still requires the
default tree-walking interpreter.
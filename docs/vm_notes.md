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

Implemented: number literals, string literals, unary `-`, binary
`+ - * /` with correct precedence and left-associativity, parenthesized
grouping, `print`, global variables, **block statements (`{ ... }`) and
local variables with real lexical scoping** — locals are resolved to a
fixed stack-slot index at COMPILE time (a single array access at
runtime), not looked up by name like globals still are. Shadowing,
nested blocks, and the "can't reference a variable in its own
initializer" (`var a = a;`) and "no duplicate declaration in the same
scope" footguns are all handled correctly.

Not yet implemented (in rough build order): `if`/`while`/`for` (control
flow needs jump opcodes, not yet added), functions and closures,
classes/inheritance, string concatenation via `+` and boolean
literals/logic, arrays/maps, and a real garbage collector (deferred —
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

## How to run it

```bash
./mylang --vm script.mylang     # run a file through the VM
./mylang --vm                    # VM REPL
./mylang script.mylang           # tree-walking interpreter (full language, default)
```

The two engines are completely independent — `--vm` is currently a
small subset of the language; everything else still requires the
default tree-walking interpreter.
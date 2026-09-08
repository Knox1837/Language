# mylang - Bytecode VM Notes

Status: in progress. This documents the VM's internal design, separate
from `language-spec.md` (which documents the LANGUAGE - the VM is
purely an alternate execution engine for the same language, invisible
to anyone writing `.mylang` scripts once it's complete).

## Why a second execution engine

The tree-walking interpreter (`src/interpreter/`) works by directly
walking the AST every time a script runs - each node dispatches through
a virtual function call, and the same node gets re-interpreted from
scratch every time it executes (e.g. every loop iteration). This is
simple to build and reason about, which is why it came first, but it's
slower than it could be: virtual dispatch overhead, poor cache locality
from a pointer-heavy tree structure, and no reuse of work across repeated
executions of the same code.

A bytecode VM (`src/vm/`) fixes this by splitting execution into two
phases: **compile** (walk the source once, ahead of time, into a flat
array of simple instructions) and **run** (a tight loop that reads those
instructions and acts on them using a value stack - no tree, no
recursion into node objects, just array indexing and a switch
statement). This is how CPython, Lua, and the JVM all work internally.

## Current status

Implemented: number literals, unary `-`, binary `+ - * /` with correct
precedence and left-associativity, parenthesized grouping, `print`.

Not yet implemented (in rough build order): variables, `if`/`while`/`for`,
functions and closures, classes/inheritance, strings/bools/nil as real
values (currently `VMValue` is just `double`), arrays/maps, and a real
garbage collector (deferred - likely stays on `shared_ptr`/reference
counting initially, same as the tree-walker, until/unless GC becomes a
specific goal of its own).

## Architecture

- **`opcode.h`** - the instruction set. One enum value per instruction;
  some are followed by an operand byte (e.g. `OP_CONSTANT`'s index into
  the constant pool).
- **`vm_value.h`** - the VM's own value type, deliberately SEPARATE from
  the tree-walker's `Value` (`src/interpreter/value.h`). Currently just
  `double`; will grow into its own small variant as more types are
  added, rather than adopting the tree-walker's `Value` wholesale (which
  would drag in `Callable`/`LoxInstance`/etc. before the VM has any use
  for them).
- **`chunk.h`/`.cpp`** - one compiled unit: a flat byte array (`code`), a
  constant pool (`constants`), and a parallel line-number array
  (`lines`) for error reporting.
- **`compiler.h`/`.cpp`** - a single-pass Pratt parser: reads tokens
  (reusing the existing `Lexer`/`Token` from `src/lexer/`) and emits
  bytecode directly, with no separate AST step. Precedence is handled
  via a per-token-type table (`getRule()`) rather than the tree-walker's
  ladder of `equality()`/`comparison()`/`term()`/`factor()` functions -
  this is the standard technique for a bytecode compiler's front end
  (same approach as `clox` in *Crafting Interpreters*).
- **`vm.h`/`.cpp`** - the execution loop itself: reads one instruction at
  a time via an instruction pointer (`ip`) into `chunk.code`, acting on
  a `std::vector<VMValue>` stack.

## Design decision: separate compiler, not reusing the tree-walker's AST

The tree-walker's `ExprVisitor`/`StmtVisitor` interfaces now have
~15 pure virtual methods each (covering functions, classes, arrays,
maps, imports, ...). Building the VM's compiler as a partial
implementation of those interfaces would mean writing "not yet
supported" stub overrides for everything the VM doesn't handle yet -
significant boilerplate that provides no value until the VM actually
catches up feature-for-feature. A direct token-to-bytecode compiler
avoids this entirely and is free to grow at its own pace, one language
construct at a time, independent of how big the tree-walker's AST has
grown.

## How to run it

```bash
./mylang --vm script.mylang     # run a file through the VM
./mylang --vm                    # VM REPL
./mylang script.mylang           # tree-walking interpreter (full language, default)
```

The two engines are completely independent - `--vm` is currently a
small subset of the language; everything else still requires the
default tree-walking interpreter.
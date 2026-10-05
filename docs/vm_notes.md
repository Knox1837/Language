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
`+ - * / %` with correct precedence and left-associativity (`+` also
concatenates two strings; any other mix of operand types is a runtime
error, exactly as in the tree-walker), comparisons
(`== != > >= < <=`), logical `and`/`or` (short-circuiting, returning an
actual operand value like the tree-walker — not necessarily a bool),
parenthesized grouping, `print`, global and local variables with real
lexical scoping, control flow (`if`/`else`, `while`, `for`), functions
with full recursion (including mutual recursion), and now **closures**:
a nested function can capture a variable from an enclosing function and
keep working correctly even after the enclosing call has returned
(`var c = makeCounter(); c(); c();`), multiple closures capturing the
SAME variable observe each other's mutations to it (not independent
copies), and capturing chains correctly through more than one level of
nesting (a function capturing a variable from its grandparent). Every
callable value at runtime is now a closure (`VMClosure`) — even a plain
non-capturing function is just a closure with zero captured upvalues —
and the top-level script itself is wrapped in one too, for uniform
call-frame handling.

The VM also has **native functions** (C++ code callable from a script,
see "Design note: native functions" below): everything in the
tree-walker's standard library that doesn't need arrays or maps. Math:
`clock abs sqrt pow floor ceil round min max sin cos tan log log10
random randomInt setSeed`, plus the constants `PI` and `E`. String: `len
str upper lower substring charAt find startsWith endsWith trim replace
toNumber`. Type: `isNumber isString isBool isNil isFunction`. I/O:
`input`. `print` and `str()` share one value formatter that matches the
tree-walker's output.

Compound assignment (`+= -= *= /= %=`) works on any variable — local,
captured (upvalue) or global; see "Design note: `%` and compound
assignment" below.

Not yet implemented (in rough build order): arrays/maps together with
the collection side of the standard library (`split`, `join`, `isArray`,
`isMap`, and the array and map functions), including compound assignment
on elements and fields (e.g. `a[i] += 1`), classes/inheritance, imports,
and a real garbage collector — closures make this considerably more
relevant than before, since `shared_ptr` reference counting cannot
detect or collect a REFERENCE CYCLE (e.g. a closure that captures a
variable which itself ends up holding a reference back to that same
closure); this remains a known, deferred limitation rather than a
correctness bug affecting any currently-supported program shape.

## Architecture

- **`opcode.h`** — the instruction set. One enum value per instruction;
  some are followed by an operand byte (e.g. `OP_CONSTANT`'s index into
  the constant pool).
- **`vm_value.h`** — the VM's own value type, deliberately SEPARATE from
  the tree-walker's `Value` (`src/interpreter/value.h`). A
  `std::variant` of nil / number / bool / string / `VMFunction` /
  `VMClosure` / `VMNative`. Strings back both variable-name constants
  and real string literals. `VMFunction` only ever appears in a
  chunk's constant pool (compiled code); the runtime callable is the
  closure. The type will keep growing as more types are added, rather
  than adopting the tree-walker's `Value` wholesale (which would drag
  in `Callable`/`LoxInstance`/etc. before the VM has any use for
  them). Nothing uses `std::visit` on it: values are inspected through
  the `isVM*`/`asVM*` helpers, so adding an alternative does not break
  existing code.
- **`vm_function.h`** — a compiled function: its own `Chunk`, `arity`,
  `upvalueCount`, `name`.
- **`vm_closure.h`/`vm_upvalue.h`** — a function plus the variables it
  captured; see "Design note: closures and upvalues".
- **`vm_native.h`** — `VMNative`, a C++ function callable from a script.
- **`vm_stdlib.h`/`.cpp`** — the native functions themselves, grouped
  like the tree-walker's libs (math, string, type, io), plus
  `stringifyVMValue()`, the formatter shared by `print` and `str()`.
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

## Design note: call frames and stack-slot 0

Before functions existed, the VM had ONE flat `chunk`+`ip` pair for the
entire program. Functions need each call to have its OWN instruction
pointer (into that function's own compiled `Chunk`) and its own "window"
into the single shared value stack — that's a `CallFrame`
(`stackBase` + `ip` + which function it's running). `VM::run()` now
always operates on `frames.back()`.

A subtlety worth calling out: slot 0 of EVERY function's locals —
including the top-level script, which is itself compiled as a
zero-argument "function" so it can be pushed onto the same call-frame
stack uniformly — is RESERVED for the callable value itself, not
available for a user's first real local/parameter. This has to be
consistent between the compiler (which assigns compile-time slot
numbers to locals) and the VM (which lays out the runtime stack) or
they silently disagree about what's in slot 0 — see the bug below,
which is exactly that disagreement.

Recursion works with no special-casing: a global function's name is
resolved by NAME at CALL time (`OP_GET_GLOBAL`), and by the time a
recursive call inside a function's body actually executes, that
function's `OP_DEFINE_GLOBAL` has long since run (the whole `def`
statement completes — including binding its own name — before the
function is ever invoked). Mutual recursion between two functions works
for the same reason.

## Bug caught during testing: stack slot 0 collision

Symptom: `{ var y = "inner"; print y; }` printed `<fn <script>>`
instead of `"inner"`, and every statement after that block silently
misbehaved. Root cause: the top-level script's `CallFrame` was created
with `stackBase = 0`, and the script's own function VALUE was pushed
onto the stack at slot 0 (`VM::interpret()`) — but the COMPILER didn't
know slot 0 was taken, so it happily assigned slot 0 to the block's
first declared local (`y`), and `OP_GET_LOCAL 0` read the script
function value instead. Worse, real function CALLS used a different,
inconsistent convention (`stackBase` pointing at the first ARGUMENT,
not the callee) — so top-level code and function bodies disagreed
about what "slot 0" even meant.

Fixed by making both sides agree on one rule, uniformly: slot 0 is
always reserved for the callable value itself (the compiler reserves it
with a placeholder `LocalVar` no real identifier can match; the VM's
`call()` points `stackBase` at the callee, not the first argument).
Caught via a regression test that re-ran every earlier increment's
tests together — the isolated function tests alone hadn't exercised a
BLOCK-scoped local sitting at the very first available slot, which is
exactly the case that exposed the mismatch.

## Design note: closures and upvalues

A `VMFunction` alone is just compiled code with no captured state. A
`VMClosure` wraps one plus the SPECIFIC variables it captured at the
moment it was created (`vm_closure.h`) — every callable value at
runtime is a closure now, even a plain non-capturing function (zero
upvalues) and the top-level script itself (built directly in
`VM::interpret()` rather than via bytecode, since nothing encloses it).

An upvalue (`vm_upvalue.h`) has two states:
- **Open** — the enclosing call is still running, so the captured
  variable still lives on the shared VM stack. The upvalue just
  remembers WHICH stack slot to read/write.
- **Closed** — the enclosing call has returned (or the block containing
  the captured local has ended). The value has been copied out into the
  upvalue's own storage, independent of the stack from that point on.

**Why stack INDEX, not a raw pointer, for the open case:** the VM's
stack is a `std::vector<VMValue>`, which can reallocate — move every
element to a new memory block — whenever it grows. A raw `VMValue*`
taken into it would dangle the moment that happens. An index into the
vector stays valid across any such reallocation, since it's resolved
fresh through `stack[index]` every time it's read or written, rather
than dereferencing a stored address. (clox avoids this differently — its
VM stack is a fixed-size C array that never reallocates — but since this
VM already uses index-based access everywhere else, indices are the
more consistent fix here too.)

**Why closures sharing a captured variable actually share it:**
`VM::captureUpvalue()` first checks whether an OPEN upvalue already
exists for the exact stack slot being captured, and returns that SAME
object if so, rather than creating a new one. Two nested functions
(`inc` and `show` in the test below) that both capture the same
enclosing `x` therefore end up holding the exact same `VMUpvalue`
object — a write through one is visible through the other, because
there's only one underlying object, not two independent copies.

**Multi-level capturing** (a function capturing a variable from its
*grandparent*, not its immediate parent) works via `isLocal = false`
upvalue entries: `Compiler::resolveUpvalue()` recurses outward through
`functionStack` by INDEX (never by stored pointer — `push_back` can
reallocate that vector too, same reasoning as the runtime stack above),
and each function in the chain registers its own upvalue entry pointing
either at a direct local (`isLocal = true`) or at an upvalue its own
immediately-enclosing function already resolved (`isLocal = false`,
chaining through).

**Tested explicitly** (not just the classic single-counter example):
two independent closures from separate calls to the same outer function
have independent state; two DIFFERENT closures from the SAME call share
a mutation to their commonly-captured variable; capturing works through
a block's scope-exit (`OP_CLOSE_UPVALUE`), not just a function's
`OP_RETURN`; and a 3-level-deep capture chain (`outer` → `middle` →
`inner`) resolves correctly; and a closure over a function PARAMETER
that outlives its call (`adder(5)` returning a function that adds 5) —
the case that exposed the return-time bug described below.

## Bug caught during testing: captured variables never closed on return

Symptom: on MSVC Debug, running the basic closure test
(`makeCounter`) aborted with "vector subscript out of range". On
unchecked builds the same bug gave silently WRONG output instead:
`adder(5)(1)` and `adder(10)(1)` printed `2` and `2` instead of `6` and
`11`.

Root cause: a block-scoped local that gets captured is closed by
`OP_CLOSE_UPVALUE` when the block ends. But a function BODY has no
`endScope()` (its locals are released all at once when `OP_RETURN`
truncates the stack), so nothing ever closed the upvalues pointing into
the returning call's stack slots. `OP_RETURN` carried a comment saying
it should, but the `closeUpvalues(...)` call itself was missing. After
`stack.resize(...)`, a still-open upvalue held an index past the end of
the stack: a checked `std::vector::operator[]` aborts, while an
unchecked one reads whatever stale value is still sitting in the
vector's spare capacity — which often happens to look right, so simple
tests can pass by luck.

Fix: `OP_RETURN` calls `closeUpvalues(returningFromStackBase)` BEFORE
popping the frame and truncating the stack, so each captured variable
is copied into its upvalue while it still exists.

Why it was missed: AddressSanitizer and `_GLIBCXX_ASSERTIONS` both ran
clean, because the stale read stayed inside the vector's capacity.
`-D_GLIBCXX_DEBUG` (fully checked containers) reproduced the abort
immediately. Lesson: run VM tests with checked containers as well as
the sanitizers, and include a test that captures a PARAMETER, not just
a local declared inside the function.

## Design note: native functions

A native function is C++ code a script can call. `VMNative`
(`vm_native.h`) holds a `name`, an `arity`, and an `fn` with the
signature `bool(args, result, error)`: on success it writes the return
value to `result` and returns true; on failure it fills `error` and
returns false. Natives do not throw and do not know the current line —
`VM::callValue` turns a failure into `runtimeError(error)`, which
attaches the line of the CALL. (The tree-walker prints `[line 0]` for
errors raised inside natives; the VM reports the real line.)

Calling a native uses no `CallFrame`: `callValue` checks the arity
(same "Expected N arguments but got M." message as for user functions),
copies the arguments into a vector, runs `fn`, then drops the arguments
and the callee from the stack and pushes the result. It copies the
`shared_ptr<VMNative>` first, because the `callee` reference points
into the stack that is resized afterwards.

Registration happens in the `VM` CONSTRUCTOR (`registerVMStdlib`), not
in `interpret()`, so a REPL user who redefines `len` is not overwritten
on the next line. Names, arities and error messages mirror
`src/stdlib/` so both engines fail the same way. Adding a native is one
`define(...)` call in `vm_stdlib.cpp` (or `defineMath1(...)` for a plain
number-to-number function).

`PI` and `E` are ordinary globals holding numbers, not functions, so a
script writes `PI`, not `PI()` — and, as in the tree-walker, they are
not protected from reassignment. The random functions share one
`std::mt19937` for the whole program, and `setSeed(n)` makes
`random()`/`randomInt()` reproducible. The generated numbers are the
same as the tree-walker's when both are built with the same C++
standard library, but `std::uniform_real_distribution` is not specified
to the bit, so MSVC and libstdc++ produce different sequences for the
same seed. Tests therefore check properties (same seed gives the same
value, results stay in range) instead of exact numbers.

Behaviors inherited from the tree-walker that can surprise: `len` and
`charAt` count BYTES, so a multi-byte UTF-8 character counts as several
and `upper`/`lower` only change ASCII letters; `toNumber` accepts
whatever `std::stod` does (`"0x1A"` is `26`, `"inf"` and `"nan"` parse,
leading whitespace is skipped) but rejects trailing characters, so
`" 5 "` is an error; and index arguments such as the ones to `substring`
and `charAt` are truncated toward zero, so `charAt("hello", 1.9)` is
`"e"`.

**Shared value formatting.** `stringifyVMValue()` is used by both
`print` and `str()`, and reproduces the tree-walker's `stringifyValue`:
whole numbers print as integers (`10`), other numbers via
`std::to_string` (`3.140000`). Earlier, `print` used `cout << double`
(`3.14`), so the two engines disagreed on non-integer output and
`str(3.14)` would have disagreed with `print 3.14`.

## Design note: `+` and unknown opcodes

`OP_ADD` accepts number+number (adds) and string+string (concatenates);
anything else reports "Operands must be two numbers or two strings.",
the tree-walker's exact message. The other arithmetic and comparison
operators remain number-only through `requireNumbers`.

`run()`'s `switch` ends with a `default:` case that reports
`Unknown opcode N` as a runtime error. It is only reachable through a VM
or compiler bug (such as a new opcode with no `case`), never from a user
script. Side effect: with a `default:` present, the C++ compiler no
longer warns about unhandled enum values (GCC `-Wswitch`, MSVC C4062),
so when adding an opcode, check that it has a `case` in `run()`.

## Design note: `%` and compound assignment

`%` is its own opcode (`OP_MODULO`) built on `fmod`, not an integer
`%`, because the only numeric type is a double: the result takes the
sign of the dividend (`-7 % 3` is `-1`, `5.5 % 2` is `1.5`), and a zero
divisor is the runtime error "Modulo by zero.". It shares `*` and `/`'s
precedence through the same parse rule, so `2 + 7 % 4 * 2` groups the
way it does in the tree-walker.

Compound assignment (`+= -= *= /= %=`) adds no new opcodes.
`Compiler::variable()` compiles `x op= value` as: read `x`, compile
`value`, apply the operator's opcode, store back — the same bytes
`x = x op value` produces. It therefore works identically for locals,
upvalues and globals, `+=` concatenates strings, and every operator
inherits the type and zero checks of its plain form. Behavior matches
the tree-walker: `x` is read BEFORE the right side runs, the right side
is a full expression (`x *= 2 + 3` multiplies by 5), chains are
right-associative (`a += b += 3`), and the expression's value is the
stored result (the `OP_SET_*` opcodes peek rather than pop, so
`print x += 1;` prints the new value).

A leftover compound operator after something that isn't a plain
variable (`1 += 2`, `a + b += c`, `f() += 1`) is reported as "Invalid
compound assignment target.", mirroring the stray-`=` check at the end
of `parsePrecedence`. As with `=`, the VM compiler has no panic mode,
so the same mistake also prints a follow-on "Expect ';'" error.

Plain variables are the only targets so far, which is why naming the
target twice (once to read it, once to write it) is harmless. Arrays
and instances will need more: `a[i] += v` and `obj.f += v` must
evaluate `a`, `i` and `obj` ONCE, which is why the tree-walker has
dedicated `CompoundIndexSet`/`CompoundSet` nodes. Those targets will
need their own compilation path (for example, duplicating the target
values on the stack) instead of this read-then-write shortcut.

## How to run it

```bash
./mylang --vm script.mylang     # run a file through the VM
./mylang --vm                    # VM REPL
./mylang script.mylang           # tree-walking interpreter (full language, default)
```

The two engines are completely independent — `--vm` is currently a
small subset of the language; everything else still requires the
default tree-walking interpreter.
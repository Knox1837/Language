# mylang - Bytecode VM Notes

Status: the VM covers the whole language (closures, classes, arrays, maps,
imports, the standard library) and has a cycle collector. This documents the
VM's internal design, separate from `language_spec.md` (which documents the
LANGUAGE — the VM is purely an alternate execution engine for the same
language, invisible to anyone writing `.mylang` scripts apart from the
differences listed under "Known differences from the tree-walker").

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
see "Design note: native functions" below): the tree-walker's whole
standard library. Math: `clock abs sqrt pow floor ceil round min max sin
cos tan log log10 random randomInt setSeed`, plus the constants `PI` and
`E`. String: `len str upper lower substring charAt find startsWith
endsWith trim replace toNumber split join`. Type: `isNumber isString
isBool isNil isArray isMap isFunction`. Array: `push pop length contains
indexOf sort reverse slice binarySearch`. Map: `keys values hasKey
remove` (and `length` accepts maps too). I/O: `input`. `print` and
`str()` share one value formatter that matches the tree-walker's output.

Compound assignment (`+= -= *= /= %=`) works on any variable — local,
captured (upvalue) or global; see "Design note: `%` and compound
assignment" below.

**Arrays** work end to end: literals (`[1, "two", [3]]`), indexing
(`a[i]`), element assignment (`a[i] = v`), compound assignment on
elements (`a[i] += v`, with the target evaluated once), method syntax
(`a.push(x)`, `a.sort().reverse()`, and bound methods like `var p =
a.push; p(1)`), and every array operation also available as a plain
function (`push(a, x)`). See "Design note: arrays" below.

**Maps** work the same way: literals (`{"a": 1, "b": [2]}`,
string-literal keys only), lookup and insertion by key (`m["a"]`,
`m["c"] = 3`), compound assignment on entries (`m["a"] += 1`), method
syntax (`m.keys()`, `m.hasKey("a")`), and the same operations as plain
functions (`keys(m)`). Entries print and iterate in sorted key order.
See "Design note: maps" below.

**Classes** work too: `class Name < Super { ... }`, instances created by
calling the class (running `init` if there is one), fields created on
assignment (`obj.x = 1`, `obj.x += 1`), methods that use `this`, bound
methods that remember their instance (`var f = obj.method`), single
inheritance, and `super.method()`. See "Design note: classes" below.

**Imports** work: `import "file.mylang" as name;` runs the file once in
its own global scope and binds `name` to a module object, `name.thing`
reads the file's top-level `def`/`class`/`var` bindings, the same file
imported twice (under any spelling of its path) is the same cached
module, and circular imports are an error. See "Design note: imports
and modules" below.

**Garbage collection** works: memory is still freed by `shared_ptr`
reference counting, and a cycle collector (`gc.h`/`gc.cpp`) finds and
frees what counting cannot, namely reference cycles (a closure that
captures a variable holding itself, `push(a, a)`, `m["self"] = m`,
`obj.me = obj`, an instance holding one of its own bound methods). It
runs by itself during loops and calls, once more when the VM is
destroyed, and on demand through the VM-only natives `gc()` and
`heapSize()`. See "Design note: garbage collection" below.

## Architecture

- **`opcode.h`** — the instruction set. One enum value per instruction;
  some are followed by an operand byte (e.g. `OP_CONSTANT`'s index into
  the constant pool). `OP_WIDE` is a prefix that widens the next
  instruction's constant index to two bytes; see "Design note: wide
  constant operands".
- **`vm_value.h`** — the VM's own value type, deliberately SEPARATE from
  the tree-walker's `Value` (`src/interpreter/value.h`). A
  `std::variant` of nil / number / bool / string / `VMFunction` /
  `VMClosure` / `VMNative` / `VMArray` / `VMMap` / `VMClass` /
  `VMInstance` / `VMBoundMethod` / `VMModule`. Strings back both
  variable-name constants and real string literals. `VMFunction` only
  ever appears in a chunk's constant pool (compiled code); the runtime
  callable is the closure. The type grew one case at a time as the VM
  needed it, rather than adopting the tree-walker's `Value` wholesale
  (which would have dragged in `Callable`/`LoxInstance`/etc. before the
  VM had any use for them). Nothing uses `std::visit` on it: values
  are inspected through the `isVM*`/`asVM*` helpers, so adding an
  alternative does not break existing code.
- **`vm_function.h`** — a compiled function: its own `Chunk`, `arity`,
  `upvalueCount`, `name`.
- **`vm_closure.h`/`vm_upvalue.h`** — a function plus the variables it
  captured; see "Design note: closures and upvalues".
- **`vm_native.h`** — `VMNative`, a C++ function callable from a script.
- **`vm_array.h`** — `VMArray`, a `std::vector<VMValue>` shared by
  reference.
- **`vm_array_lib.h`/`.cpp`** — the array operations, written once and
  exposed both as free functions and as bound methods.
- **`vm_map.h`** — `VMMap`, a sorted `std::map<std::string, VMValue>`
  shared by reference.
- **`vm_map_lib.h`/`.cpp`** — the map operations, written once and
  exposed both as free functions and as bound methods.
- **`vm_class.h`** — `VMClass` (name, superclass, method table),
  `VMInstance` (class + field table) and `VMBoundMethod` (a method plus
  the instance it was read from).
- **`gc.h`/`gc.cpp`** — `GCObject`, the base class of every heap object
  that can be part of a cycle, the list of all of them, and the
  collector; see "Design note: garbage collection".
- **`vm_module.h`** — `VMModule`, one file's global namespace (a table
  of globals plus the resolved path); see "Design note: imports and
  modules".
- **`vm_stdlib.h`/`.cpp`** — the native functions themselves, grouped
  like the tree-walker's libs (math, string, type, io), plus
  `stringifyVMValue()`, the formatter shared by `print` and `str()`.
- **`chunk.h`/`.cpp`** — one compiled unit: a flat byte array (`code`), a
  constant pool (`constants`, which reuses equal numbers and strings,
  found through hash maps, up to 65536 entries — see "Known limits"), and a parallel line-number array (`lines`) for
  error reporting.
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
which is exactly that disagreement. (Classes later reuse the same
reserved slot: in a METHOD it holds the receiver, `this`, instead of the
closure. See "Design note: classes".)

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

For a plain variable, naming the target twice (once to read it, once to
write it) is harmless. Array elements, map entries and instance fields
are different: `a[i] += v` must evaluate `a` and `i` ONCE (`a[next()] +=
1` must call `next()` once), which is why the tree-walker has dedicated
`CompoundIndexSet`/`CompoundSet` nodes. The VM solves it with a stack
trick instead of new nodes: `OP_DUP2` for elements and entries (see
"Design note: arrays") and `OP_DUP` for fields (see "Design note:
classes").

## Design note: arrays

**Representation.** `VMArray` (`vm_array.h`) is a `std::vector<VMValue>`
held through a `shared_ptr` inside the value variant, so arrays have
REFERENCE semantics like the tree-walker's: `var b = a;` makes `b`
another name for the same array, and `==` compares identity (`[1] ==
[1]` is `false`, `a == a` is `true`). A literal builds a fresh array
every time it runs. Arrays are truthy, including empty ones.

**Literals.** `[a, b, c]` compiles each element in order, then
`OP_ARRAY n`. The count is a 2-byte operand (like jump offsets), so a
literal can hold up to 65535 elements; more is a compile error. The
elements are simply left on the value stack and gathered from there,
so a literal's size never touches the constant pool (only the distinct
numbers and strings inside it do).

**Indexing.** `a[i]` is `OP_GET_INDEX`; `a[i] = v` is `OP_SET_INDEX`,
which pushes the assigned value back so assignment stays an expression
(`a[0] = a[1] = 9` works). Error messages are the tree-walker's:
"Only arrays, maps and strings can be indexed.", "Array index must be a
number.", "Array index out of range.".
A string can be indexed for reading: `s[i]` is a one-byte string, like
`charAt` (so it indexes BYTES, not UTF-8 characters), with the same
truncation rule and the errors "String index must be a number." and
"String index out of range.". Writing to one, `s[i] = x` or `s[i] += x`, is the
runtime error "Strings are immutable: can't assign to a string index."; for
`+=` both engines still run the read, the right side and the operator first,
and refuse only at the write. The tree-walker converts a string index with a
check on the double before any cast, so NaN and huge values are out of range
there too. An index
is truncated toward zero, so `a[1.9]` is `a[1]` and `a[-0.5]` is
`a[0]`; NaN, infinity and absurdly large values are simply out of range
(the tree-walker casts them to `int`, which is undefined behavior).

**Compound assignment on elements.** `a[i] += v` must evaluate `a` and
`i` exactly once. The compiler emits `OP_DUP2` (which turns
`[a i]` into `[a i a i]`), then `OP_GET_INDEX` through the copy
(leaving `[a i current]`), the right side, the arithmetic opcode, and
finally `OP_SET_INDEX` through the original pair. The type and range
checks happen in that first `OP_GET_INDEX`, so a bad target fails BEFORE
the right side runs, exactly like the tree-walker (`a[3] += f()` never
calls `f`).

**Methods.** `.` is an infix rule at the same precedence as a call and
compiles to `OP_GET_PROPERTY name`. On an array it produces a native
function already BOUND to that array (what the tree-walker's
`getArrayMethod` does), which the ordinary `OP_CALL` then invokes:
`a.push(1)`. Because the binding is captured, `var p = a.push; p(1);`
also appends to `a`. Every property access creates a new bound native,
so `a.push == a.push` is `false`; an `OP_INVOKE` that fuses the lookup
and the call would avoid the allocation, and is deferred until it
matters. A missing method is an error at the access ("Undefined array
method 'x'."), before any call. Assigning through a dot (`a.x = 1`,
`a.x += 1`) is the runtime error "Only instances have fields.", as in
the tree-walker (it was a compile error before classes existed).

**One implementation, two forms.** `vm_array_lib.cpp` holds each
operation once, taking the array plus the arguments after it. The free
function (`push(a, x)`, 2 arguments) checks "Expected an array
argument." and calls it; the method (`a.push(x)`, 1 argument) is the
same function with the array captured. `length` is the one free function
shared with maps: it accepts either, and its error says "Expected an
array or map argument.". `split`, `join` and `isArray` live in
`vm_stdlib.cpp` with the other string and type functions.

**`sort` and `binarySearch`.** Elements must be all numbers or all
strings, else "Can only sort arrays of all-numbers or all-strings."
(with fewer than two elements nothing is compared, so any type is
accepted). `sort` checks the whole array BEFORE sorting, because natives
report errors by return value rather than by throwing from inside
`std::sort`; the tree-walker throws mid-sort and can leave the array
partly shuffled, while here a failed sort leaves it untouched. NaN sorts
after every other number, which keeps the ordering valid for
`std::sort`.

**Printing.** `[1, 2, 3]`, with strings WITHOUT quotes (`[a, b]`),
matching the tree-walker. An array that contains itself prints `[...]`
at the repeat instead of recursing forever (the tree-walker overflows
its stack on that).

## Design note: maps

**Representation.** `VMMap` (`vm_map.h`) is a
`std::map<std::string, VMValue>` behind a `shared_ptr`. Like arrays,
maps have REFERENCE semantics (`var b = a;` aliases the same map, `==`
compares identity, `{} == {}` is `false`) and are always truthy. Keys
are strings only. Because the container is a sorted `std::map`,
`keys()`, `values()` and printing come out in byte-wise key order, NOT
insertion order (`"B"` sorts before `"a"`), exactly like the
tree-walker.

**Literals.** `{"a": 1, "b": 2}` compiles each entry as its key (a
string constant) followed by its value, then `OP_MAP n` with a 2-byte
entry count. Keys must be string LITERALS: `{a: 1}` and `{1: 2}` are
compile errors ("Expect string key in map literal."). Values are
evaluated in source order, and a repeated key keeps its LAST value
(`{"a": 1, "a": 2}` is `{"a": 2}`). A `{` at the start of a statement
is always a block, in both engines, so a map literal needs an
expression context (`var m = {};`, `print {...};`).

**Indexing.** Maps share `OP_GET_INDEX`/`OP_SET_INDEX` with arrays,
dispatching on the object. Reading requires a string key ("Map key must
be a string.") that exists ("Undefined map key 'k'."); writing INSERTS
a new key or replaces an existing one, because maps grow freely where
arrays have fixed positions. `m["k"] += v` goes through the same
`OP_DUP2` sequence as arrays, so the lookup and its missing-key error
happen BEFORE the right side runs and `m[nextKey()] += 1` calls
`nextKey()` once. As in the tree-walker that makes `m["new"] += 1` an
error ("Undefined map key") rather than a way to create the key.

**Methods, never entries.** `.name` on a map always means a method
(`keys values hasKey remove length`), never an entry: `m.a` is
"Undefined map method 'a'." even when the key `"a"` exists. Entries are
reached with `m["a"]`. Method access builds a bound native exactly like
arrays do (`var k = m.keys; k()`), and `m.x = 1` is the runtime error
"Only instances have fields.", the same as on arrays.

**One implementation, two forms.** `vm_map_lib.cpp` mirrors
`vm_array_lib.cpp`: `keys(m)`/`m.keys()`, `values`,
`hasKey(m, k)`/`m.hasKey(k)`, and `remove(m, k)`/`m.remove(k)` (true if
the key existed). `length` exists as a method in each library, while
the free `length(x)` is a single function in `vm_array_lib.cpp` that
accepts an array or a map. `keys()` and `values()` return NEW arrays
(snapshots): later changes to the map don't show up in an array already
returned. Errors: "Expected a map argument." from the free functions,
"Key must be a string." from `hasKey`/`remove`, and "Undefined map
method 'x'." on a bad method name.

**Printing.** `{"a": 1, "b": text}`: keys quoted, values printed like
top-level values (so strings appear without quotes), entries in key
order. A map that contains itself prints `{...}` at the repeat, using
the same guard as the array `[...]`, so arrays and maps nested in each
other in a cycle terminate too.

## Design note: classes

**Objects.** `vm_class.h` holds three small types, each behind a
`shared_ptr` in the value variant: `VMClass` (name, optional
superclass, a table of method closures), `VMInstance` (its class plus a
table of fields) and `VMBoundMethod` (a method closure plus the
instance it was read from). As with arrays and maps they have
REFERENCE semantics: `==` compares identity, and they are always
truthy. They print as `<class Name>`, `<instance of Name>` and, for a
bound method, like the plain function it came from (`<fn name>`).
`isFunction` is true for classes and bound methods as well as for
functions, because the tree-walker treats every Callable alike.

**Declaring a class.** `class A < B { ... }` compiles to `OP_CLASS "A"`
(a new, empty class) and the usual variable definition, then, if there
is a superclass, a push of `B` and `OP_INHERIT`; then the class is
pushed again and each method is an `OP_CLOSURE` followed by `OP_METHOD
"name"`, and a final pop. The class name is defined BEFORE the body is
compiled, so methods can mention their own class. Methods are compiled
by the same code as functions (same upvalue machinery), as a METHOD or,
when named `init`, an INITIALIZER.

**Slot 0 holds the receiver.** The compiler reserves slot 0 of every
function for the callee (see "call frames and stack-slot 0"). Methods
reuse that reservation: for a METHOD or INITIALIZER the slot is NAMED
`this`, and the call puts the instance there instead of the closure. So
`this` is just a local variable, and a function nested inside a method
captures it through the existing upvalue mechanism. That is how
`getter() { def inner() { return this.v; } return inner; }` keeps
working after `getter` has returned, and why closing captured variables
on `OP_RETURN` (see the bug note above) matters for classes too.

**Calling.** `OP_CALL` on a class replaces the class in its stack slot
with a fresh instance. If the class or an ancestor has an `init`, it
runs as an ordinary call whose slot 0 is that instance; with none there
is nothing to run, and the instance already sits where the call's value
belongs (a non-zero argument count is then "Expected 0 arguments but
got N."). Calling a bound method puts its receiver in the slot and runs
the plain closure. A class's arity is its `init`'s, found through the
inheritance chain, and arity errors use the usual "Expected N arguments
but got M." wording.

**Initializers.** In an `init`, falling off the end, `return;` and
`return expr;` all return `this` (slot 0). Like the tree-walker, a
returned expression is still EVALUATED, for its side effects, and then
discarded; some languages reject it. Calling `obj.init(...)` again is
allowed and returns the instance.

**Properties.** `OP_GET_PROPERTY` on an instance checks its fields
first and then the class chain; a method found there is bound to the
instance on the spot. So a field shadows a method of the same name
(`this.get = ...` hides `get()`), and `var f = obj.method; f()` keeps
its `this`. A missing name is "Undefined property 'x'.". Assignment is
`OP_SET_PROPERTY`, which creates the field on first assignment (there is
no declared field list). The tree-walker checks the target BEFORE
evaluating the right side, so the compiler emits `OP_CHECK_FIELDS`
first: `n.x = f()` on a non-instance fails with "Only instances have
fields." without calling `f`. `obj.x += v` uses `OP_DUP` so that `obj`
is evaluated once (`get().n += 1` calls `get()` once), the same idea as
`OP_DUP2` for `a[i] += v`. Each property read allocates a new bound
method, so `a.m == a.m` is `false`; an `OP_INVOKE` that fuses lookup and
call would avoid the allocation, and is deferred until it matters.

**Inheritance and `super`.** `OP_INHERIT` checks that the superclass
really is a class ("Superclass must be a class.") and links the two.
Method lookup then walks the chain at run time, like the tree-walker's
`findMethod`, so nothing is copied down. For `super`, the superclass
value left on the stack by the class declaration becomes a hidden LOCAL
named `super` that lives for the whole class body, and methods that
mention `super` capture it as an upvalue like any other variable. That
is what makes `super` mean "the superclass of the class this method was
WRITTEN in", not "of the receiver's class": with `class B < A` and
`class C < B`, calling `C().test()` where `test` is defined in `B` and
does `super.name()` reaches `A`'s `name`, although the receiver is a
`C`. `super.name` compiles to: push `this`, push `super`,
`OP_GET_SUPER`, which looks the method up starting at the superclass
and binds it to `this`.

**Checks made at compile time.** `this` is legal only lexically inside
a class body (functions nested in its methods count), and `super` also
needs a superclass: "Can't use 'this' outside of a class.", "Can't use
'super' outside of a class.", "Can't use 'super' in a class with no
superclass.", and "A class can't inherit from itself.". The tree-walker
has no such checks; it finds out at run time ("Undefined variable
'this'."). See "Known differences from the tree-walker".

## Design note: function expressions and calling script code from natives

**Function expressions** (`def(x) { ... }` in expression position) are one new
prefix rule in each engine. The VM compiles the body with the same
`functionBody()` as a named function (a flag only changes the error message for
a missing `(`) and leaves the closure on the stack, which is the value of the
expression. The tree-walker has a `FunctionExpr` node that owns a `FunctionStmt`
with an empty name and builds a `UserFunction` from it, minus defining a name.
An empty name prints as `<fn>` in both. A statement starting with `def` is still
a declaration, so an anonymous function cannot be a statement by itself.

**Recursion through a local variable.** `var g = def(n) { ... g(n - 1) ... };`
inside a function used to be a VM compile error ("Cannot read a local variable
in its own initializer") while the tree-walker, which looks names up at call
time, accepted it. A named local function avoids the error because its name is
marked initialized before its body is compiled. `varDeclaration` now does the
same when the initializer starts with `def` (and only then: any other
initializer that mentions its own variable is still the error). It is safe
because the slot is only read once the closure exists and sits in it.

**Natives that call back into script code** (`map`, `filter`, `reduce`, `sort`
with a comparator). The tree-walker's natives already receive the `Interpreter`,
so they simply call `callable->call(interpreter, args)`, and an error inside the
callback is a C++ exception passing through the native. The VM needed a real
mechanism:
- `VM::callFunction(callee, args, result)` pushes the callee and arguments like
  `OP_CALL` would, calls `callValue`, and, if that pushed a `CallFrame`, runs
  `run(baseFrames)` until that frame has returned. `run` takes the number of
  frames at entry: `OP_RETURN` returns from `run` when the frame count drops back
  to it, leaving the result on the stack. For the whole program the value is 0
  and the existing `frames.empty()` check does the job. Callees that are natives,
  or classes without `init()`, push no frame and are already finished.
- Natives are plain functions with no VM parameter, so they reach the VM through
  `VM::active()`, set (and restored) for the duration of `interpret()`.
- **Errors are reported once.** `runtimeError` prints, unwinds everything and sets
  `errorReported`. A callback that fails returns `false` from `callFunction`; the
  native returns `false` with an EMPTY message, and `callValue` skips its own
  `runtimeError` when the flag is set (it would print a second, empty error with
  line -1). `interpret()` clears the flag.
- **Snapshots.** Every operation iterates over a copy of the elements, because a
  callback is arbitrary script code and may push to the array being processed.
  `sort` sorts a copy and assigns it back only on success.
- **The collector needs nothing special.** The native's locals (the snapshot, the
  result array, the accumulator) are `shared_ptr`s, so they count as references
  from outside the heap, and collection may happen inside a nested `run`. This was
  checked with the stress build, which collects between almost every pair of
  instructions.
- **The sort** is Timsort (`util/tim_sort.h`, one header shared by both engines, used
  for `sort(arr)` and `sort(arr, cmp)`), not `std::sort`. `std::sort` assumes a
  strict weak ordering and can read out of bounds when a user comparator is
  inconsistent. See "Design note: Timsort" below.
- **Optional arguments.** `sort(arr)` and `sort(arr, cmp)` needed natives with a
  range of argument counts: `Callable::maxArity()` in the tree-walker and
  `VMNative::maxArity` in the VM (-1 means exactly `arity`). The error reads
  "Expected 1 or 2 arguments but got 3." Only `sort` uses it so far.
- A callback may itself call `map` or `sort`: the C++ stack grows by one nested
  `run` per level, so (as with deep recursion) very deep nesting can overflow it.

## Design note: Timsort

`util/tim_sort.h` is a port of CPython's `listsort` (Tim Peters's Timsort), used
for BOTH forms of `sort` in BOTH engines, so they always produce the same order.
It replaced `std::sort` for the plain `sort(arr)` and a stable merge sort that
was briefly used for comparators.

**The algorithm.**
1. Scan for natural runs: a non-descending run, or a strictly descending one,
   which is reversed in place (strictness is what keeps the sort stable).
2. A run shorter than the "minrun" (32 to 64, chosen from `n` so the final merges
   balance) is extended to that length with a binary insertion sort.
3. Runs go on a stack and are merged to keep their lengths balanced, using the
   corrected invariants of CPython 3.x and Java (the original ones were proved
   wrong in 2015).
4. A merge first trims what is already in place (`gallopRight` for where B's first
   element lands in A, `gallopLeft` for where A's last lands in B), copies the
   SHORTER run to a buffer, and merges towards it (`mergeLow` / `mergeHigh`).
   When one side wins several times in a row it switches to galloping
   (exponential search) and skips many elements per comparison; the threshold
   adapts as the sort goes.

Only `less(a, b)` ("a is strictly before b") is ever called. A comparator
`cmp(a, b)` becomes `cmp(a, b) < 0`; zero and positive both mean "not before",
which is what makes equal elements keep their order.

**Cost, measured** (optimized build, VM, the sort call alone, against what it
replaced): 400,000 random numbers 9% faster than `std::sort`; a comparator sort
of 60,000 elements 27% faster than the merge sort it replaced; 400,000
already-sorted numbers about 10x faster than `std::sort` (25 ms to 2.6 ms),
because the whole array is one run. On random data Timsort used 1.53 million
comparisons for 100,000 elements, below the n·log2(n) of about 1.66 million that a
textbook merge sort needs, which matters most when the comparator is a script
function.

**Safe with any comparator.** The comparator is user code and can be nonsense
(always true, random, cyclic). Every count and index in the merges is bounded by
the lengths involved and checked before use, as CPython does, so a bad
comparator can only give a bad ORDER: no out-of-range access, and the result is
always a permutation of the input.

**Errors.** A comparator written in the language can fail, which in the
tree-walker is a C++ exception passing through the sort and in the VM a
`failed` flag. Either way `sort(arr, cmp)` sorts a COPY and puts it in the array
only when it finishes, so the array is untouched if the comparator fails.

**The plain sort** (`sortNatural` in `array_callbacks.h`, and `opSort` in
`vm_array_lib.cpp`) first checks that the array is all numbers or all strings
(fewer than two elements are accepted whatever they are) and fails BEFORE moving
anything, then sorts in place; the ordering is total (NaN after every other
number) so it cannot fail afterwards. Before this the tree-walker used `<` for
numbers, which is not a valid ordering when a NaN is present (undefined behavior
in `std::sort`), checked types from inside the comparator, and rejected
`sort([nil])` while the VM accepted it. Both engines now share these rules.

**Testing it.** `tools/tim_sort_fuzz.cpp` is a stand-alone program (not part of
the interpreter build; the build line is in its header). It compares the sort
with `std::stable_sort` on fourteen shapes of data at sizes from 0 to 300,000
(3,052 cases), checks the comparison counts (n - 1 for sorted, reversed and
all-equal input), runs 4,000 cases with deliberately broken comparators and
checks each result is a permutation, and runs 3,000 cases where the comparator
throws part-way. Run it under AddressSanitizer and `_GLIBCXX_DEBUG` after any
change to `tim_sort.h`.

## Design note: break and continue

Both engines implement them with the same rules (see "Break and continue" in
`language_spec.md`): they target the innermost loop of the CURRENT function,
and using one outside a loop is the compile/parse error "Can't use 'break'
outside of a loop." (a function declared inside a loop starts with no
enclosing loop).

**Compiler (VM).** Each `FunctionState` has a stack of `LoopInfo`
(`compiler.h`), pushed for the body of every `while` and `for`:
- `scopeDepth`: the scope depth of the loop itself. Locals declared deeper
  than this belong to the loop body. For a `for` loop it is the depth of the
  for-scope that holds `var i`, so `break` and `continue` never touch `i`.
- `continueTarget`: where `continue` goes. For a `while` loop it is the
  condition. For a `for` loop it is the increment clause when there is one
  (the compiler already reassigns `loopStart` to the increment before it
  compiles the body, so the value is simply read at that moment), otherwise
  the condition. `continue` is therefore an ordinary `OP_LOOP` backwards.
- `breakJumps`: the forward `OP_JUMP`s emitted by `break`, patched once the
  loop is finished.

`break` and `continue` first call `emitLoopLocalsCleanup`: one `OP_POP` per
local deeper than the loop (innermost first), or `OP_CLOSE_UPVALUE` for a local
a closure has captured, the same choice `endScope()` makes. The locals are NOT
removed from the compiler's list, because the rest of the block after the jump
is still compiled in their scope, and its own `endScope()` emits the same pops
for the path that does not jump.

**Where `break` lands.** In `while`, after the final `OP_POP` that discards the
falsey condition: a `break` runs inside the body, where the condition was
already popped, so it must skip that pop. In `for`, at the same point but
BEFORE `endScope()`, because the break path still has the loop variable on the
stack and `endScope()` must pop it (and close it, if a closure captured it).

**Why compile-time `isCaptured` is enough.** A jump emitted before a closure is
compiled might not close an upvalue that closure later captures, but then the
closure is created after the jump in the same scope instance, so it cannot have
captured anything when the jump runs. A loop inside that scope that ran the
capture before the break would be the innermost loop, and the break would
target it, leaving the captured local out of its cleanup. The same reasoning
covers `continue`.

**Tree-walker.** A `for` loop used to be rewritten as `while (cond) { body;
increment; }`, which would make `continue` skip the increment. `WhileStmt`
now has a separate optional `increment` that runs after the body on every
iteration, including after `continue`, and not after `break`. `break` and
`continue` do not throw (unlike `return`): they set `Interpreter::loopSignal`,
`executeBlock` stops as soon as it sees it set, and the nearest `visitWhileStmt`
reads and resets it. They never leave their function, so a flag is enough and
costs nothing when unused. A side effect of the new `for` shape: the parser no
longer wraps body and increment in an extra block, so there is one fewer
environment allocation per iteration, and `for` loops are noticeably faster
(`bench_loop` went from 0.81s to 0.64s).

**Parse error handling.** The tree-walker's parser tracks `loopDepth` with a
small RAII guard (so an error unwinding through a loop body cannot leave it
raised). The out-of-loop error is reported but not thrown: throwing would make
`synchronize()` skip the enclosing block's closing brace and add a second,
misleading error. An empty block stands in for the statement, so a surrounding
`if` or loop body is never null.

## Design note: for-in loops

`for (var x in xs) body` (see "For-in loops" in `language_spec.md`) has its own
node in the tree-walker (`ForInStmt`) and two opcodes in the VM. The VM
compiler has no AST, so `forStatement` looks ahead after the `(`: `var`,
identifier, `in` means a for-in loop, and `forInStatement` takes over. (An
identifier followed by `in` is routed there too, so a forgotten `var` gets one
clear message instead of a cascade.)

**Bytecode.**

```
<iterable>  OP_ITER_PREP          slot A: the array to walk (hidden local " iterable")
OP_CONSTANT 0                     slot B: the position       (hidden local " position")
loopStart:  OP_ITER_NEXT -> done  pushes the next element: it is the loop variable x
            <body>
            pop x (OP_CLOSE_UPVALUE if captured)
            OP_LOOP loopStart
done:       pop position, pop array
```

- `OP_ITER_PREP` leaves an array alone and replaces a map with a new array of
  its keys (a snapshot, so inserting into the map in the body cannot invalidate
  the walk; `std::map` order is the sorted order the tree-walker uses too).
  Anything else is the runtime error `Can only iterate over arrays and maps.`,
  reported with the line of the closing `)`, like the tree-walker.
- `OP_ITER_NEXT` takes a forward jump offset like `OP_JUMP_IF_FALSE`. It reads
  the array's length on every execution, so an array that grows or shrinks
  under the loop is safe. The position is bumped in place in its stack slot.
- The two hidden locals have names containing a space, so no script can name
  them, and they are ordinary locals: the collector finds the array through the
  stack, with no new root to register.
- `x` lives in a scope of its own, one level deeper than the hidden locals,
  entered and left on every iteration. That gives each iteration a fresh stack
  slot (and a fresh upvalue if a closure captures it), which is what makes
  closures keep their own value.
- `LoopInfo.scopeDepth` is the depth of the hidden locals, so `break` and
  `continue` (which discard everything deeper) pop `x` and the body's locals and
  leave `[array position]` on the stack. `continue` jumps back to
  `OP_ITER_NEXT`. `break` and the normal exit land on the same instruction, so
  one `endScope()` after them pops the two hidden locals on both paths.

**Tree-walker.** `visitForInStmt` evaluates the iterable once. An array is
walked by index with `elements.size()` re-read each time; a map is copied into a
temporary array of keys first. Each iteration builds a new `Environment` holding
`x`, chained to the current scope, and runs the body in it with the same
save/restore-on-exception pattern `executeBlock` uses, then reads `loopSignal`
like `visitWhileStmt`.

**`in` is now a keyword**, so `var in = 1;` is a syntax error in both engines.
It is a real keyword rather than a word treated specially after `var x`, which
keeps the lexer context-free.

## Design note: garbage collection

**The problem.** Everything is reference counted (`shared_ptr`), which
frees almost every object the instant the last reference goes. It cannot
free a cycle: each member of `a = []; push(a, a);` keeps the others'
counts above zero for ever. The old limitation list was exactly the
cycles reachable from scripts: a closure that captures itself,
containers that contain themselves, an instance holding itself or one of
its own bound methods, and a local class whose methods name it.

**The approach: trial deletion over the existing counts** (the technique
of CPython's cycle collector), not a tracing collector with its own
root set. Every array, map, class, instance, bound method, closure,
upvalue, module and bound native derives from `GCObject`, which links it
into one intrusive list of all such objects. `GC::collect()` then:

1. starts each object at its `shared_ptr` use count
   (`weak_from_this().use_count()`, which reads the count without
   creating a reference);
2. calls each object's `traceChildren()` and subtracts one from every
   object it references, so what remains of an object's count is the
   number of references from OUTSIDE the heap (the VM stack, call
   frames, open upvalues, `mainModule`, `moduleCache`, and any C++ local
   holding a `shared_ptr` at that moment);
3. marks every object with a nonzero remainder as alive, and everything
   reachable from it;
4. takes a strong reference to every unmarked object, calls its
   `dropChildren()` (which clears its arrays, fields, upvalues and so
   on), and releases them all together; ordinary reference counting
   frees the pieces.

**Why this design.** The collector never has to be told what the roots
are, so a root cannot be forgotten: anything that holds a `shared_ptr`
is automatically a root, including the temporaries inside half-built
array literals, arguments of a native that is running, and values held
by C++ code in the middle of an instruction. That makes it safe to run
at ANY moment, with no "safe point" discipline, and it needed no change
to how values are stored or passed around.

**The one rule: `traceChildren()` must report exactly the `shared_ptr`s
the object holds, once each.** Missing one is harmless (the object just
looks externally referenced, so it survives and may leak). Reporting
one that does not exist could free a live object, so `collect()` checks
that no remainder went negative and, if one did, frees nothing. Raw
pointers and weak pointers are never reported, because they add nothing
to a use count: `VMClosure::module` is raw, which is also why a module
and its closures do not form a cycle.

**Bound natives.** `a.push` and `m.keys` used to capture the array or
map inside the native's `std::function`, which the collector cannot
see, so `a[0] = a.push` was an unfindable cycle. The native now holds
the receiver in its `boundReceiver` field (reported by `traceChildren`),
and its function holds only a raw pointer that it turns back into a
`shared_ptr` with `shared_from_this()` when called; the field keeps the
receiver alive for exactly as long as the native.

**Not collected objects:** numbers, strings and `VMFunction` (compiled
code holds only constants, never a closure or container) cannot be part
of a cycle and are ordinary reference-counted values.

**When it runs.** `GC::shouldCollect()` is one inlined comparison: true
once enough `GCObject`s were created since the last collection. It is
checked at `OP_LOOP` (a loop can allocate without end) and at `OP_CALL`
(so can recursion). The threshold is the larger of 10,000 allocations and
the number of objects that survived the last collection, so the total
collection work stays proportional to the allocation rate however large
the heap grows. The VM destructor drops the stack, frames, open
upvalues, the main module's globals and the module cache, then collects
once more, so even cycles that were still reachable when the program
ended are freed (a sanitizer build reports no leaks).

**Scripts can see it.** `gc()` collects now and returns how many objects
it freed; `heapSize()` returns how many `GCObject`s exist. Both are
VM-only (the tree-walker has neither), and are mainly there for tests:
see `test_vm_gc_cycles`, `test_vm_gc_alive` and `test_vm_gc_auto`.

**Stress build.** Compiling with `-DMYLANG_GC_STRESS` sets the threshold
to 1 and also checks between EVERY pair of instructions, so a collection
happens at almost every opportunity. Running the whole test suite
under that define plus `-fsanitize=address,undefined -D_GLIBCXX_DEBUG`
is the way to catch a premature free; the output of every test must be
identical to a normal build's (apart from what `gc()` itself returns,
since garbage may already be gone).

**Cost.** Each heap object grew by a vtable pointer, two list pointers,
two scratch fields and the `weak_ptr` inside `enable_shared_from_this`.
Measured against the VM before the collector (optimized build, best of
several runs): about 4% slower on a call-heavy benchmark (`fib(30)`) and
about 10% on an allocation-heavy one (2 million iterations, each making
an array and an instance). A collection is linear in the number of heap
objects.

**Adding a new heap object type:** derive from `GCObject`; implement
`traceChildren()` (every `shared_ptr` member, either with `visit(p.get())`
or `gcVisitValue(v, visit)` for a `VMValue`) and `dropChildren()`; add
it to `gcVisitValue()` in `gc.cpp` and to the `VMValue` variant; create
it with `std::make_shared`, which `collect()` relies on.

## Design note: imports and modules

`import "path" as name;` is a declaration like `var`: it works at the top
level, inside a block, and inside a function, and `name` is a global or
a local accordingly. The compiler emits `OP_IMPORT <path constant>` and
then defines `name` exactly as it would for `var name = <expr>;`, so the
module object the instruction leaves on the stack simply becomes the
variable's value.

**Every file has its own globals.** Globals used to be one table in the
`VM`. They now live in a `VMModule` (`vm_module.h`): the main script has
one (`VM::mainModule`, which survives from one REPL line to the next),
and each imported file gets its own, pre-filled with its own copy of the
standard library. That is what gives the isolation the language spec
promises: an imported file cannot see or change the importer's
variables, and `m.sqrt` works because `sqrt` is a global of `m`, exactly
as in the tree-walker, whose per-module `Interpreter` also registers the
whole standard library.

**A closure remembers its module.** `OP_GET_GLOBAL`, `OP_SET_GLOBAL` and
`OP_DEFINE_GLOBAL` use `currentFrame().closure->module->globals`, and
`OP_CLOSURE` copies the module from the enclosing closure. So a function
imported from `lib.mylang` keeps resolving names in `lib.mylang` when it
is called from anywhere, and a method of an imported class, called on an
instance made elsewhere, does too. The pointer is a raw `VMModule*`: every
module lives as long as the VM (the VM owns the main one, `moduleCache`
owns the rest), and a `shared_ptr` would form a cycle (module, its
globals, a closure, back to the module) that is never freed.

**Loading is a call, not a recursion.** The first time a path is imported,
`OP_IMPORT` resolves it, reads and compiles the file, builds a closure
for the file's top-level code and pushes an ordinary zero-argument call
frame for it. The `CallFrame` carries `importedModule`; when that frame
reaches `OP_RETURN`, the VM caches the module and pushes it (rather than
the nil the top-level code returns) as the value of the import. Nothing
calls `run()` recursively, so a long chain of imports does not use C++
stack (a chain of 300 files was tested).

**Resolution, caching, cycles:**
- A relative path resolves against the top-level script's directory (the
  current directory in the REPL), including for `import` statements
  inside an imported file; `main.cpp` passes the directory through
  `VM::setModuleBaseDir()`. `weakly_canonical` normalizes `.` and `..`,
  so `"lib.mylang"`, `"./lib.mylang"` and `"sub/../lib.mylang"` are one
  cache entry. Without a base directory `import` is a runtime error, as
  in the tree-walker.
- The cache (`moduleCache`) is filled when a file FINISHES loading, so a
  file that fails half way is not cached and is run again by a later
  import. A runtime error inside an imported file stops the program, in
  both engines (the tree-walker used to print it and carry on, with the
  half-run module cached; it now behaves like the VM, which is also what
  a circular import does).
- `currentlyLoading` holds the files whose top-level code is running; an
  import of one of them is "Circular import detected for 'path'.".
  `interpret()` clears the set, because a runtime error unwinds every
  frame and a REPL line after it must not see a file as still loading.

**Module values:** `m.name` is a lookup in the module's globals and is
live (it sees later changes the module makes to its own variables); a
missing name is "Undefined variable 'name'.", the tree-walker's message
for the same thing. Assigning through a module (`m.x = 1`, `m.x += 1`)
is "Only instances have fields.". A module prints as `<module>`, is
truthy, and is equal only to itself. `OP_IMPORT` takes a constant index,
so it is one of the instructions `OP_WIDE` can widen.

## Design note: wide constant operands (`OP_WIDE`)

A constant index used to be one operand byte, so a function could hold
256 distinct constants. Every instruction that names a pool entry
(`OP_CONSTANT`, `OP_DEFINE_GLOBAL`, `OP_GET_GLOBAL`, `OP_SET_GLOBAL`,
`OP_CLOSURE`, `OP_GET_PROPERTY`, `OP_SET_PROPERTY`, `OP_CLASS`,
`OP_METHOD`, `OP_GET_SUPER`) shares that one pool, so a separate "long"
opcode for each would have meant ten new opcodes. Instead there is one
prefix, `OP_WIDE`, that says "the NEXT instruction's constant index is
two bytes (big-endian), not one":

```
index <= 255:   OP_GET_GLOBAL  idx
index  > 255:   OP_WIDE  OP_GET_GLOBAL  hi lo
```

- **Compiler:** `emitConstantOp(op, index)` is the only place that
  emits a constant-taking instruction, and it picks the form. Small
  indexes (the common case) cost exactly what they did before.
  `makeConstant()` and `identifierConstant()` now return `size_t`.
- **VM:** `OP_WIDE` sets a flag, and `readConstant()` reads one or two
  bytes depending on it and clears it. The `OP_WIDE` case first checks
  that the next opcode is one of the ten above, so a stray prefix is a
  runtime error ("OP_WIDE before an instruction with no constant
  operand.") rather than a silently misread operand. `run()` clears
  the flag on entry.
- **OP_CLOSURE:** only its constant index is widened; the following
  (isLocal, index) byte-pairs are unchanged, because they are stack
  slots and upvalue indexes, not pool indexes.
- **Pool lookup:** with room for 65536 entries, the old linear scan in
  `Chunk::addConstant` would be quadratic, so equal strings and numbers
  are found through two hash maps (numbers by bit pattern, which keeps
  `0.0` and `-0.0` apart as before).

## Known limits

- **65536 distinct constants per function.** Numbers, strings and names
  live in the enclosing function's constant pool, and an equal number
  or string is stored ONCE and reused (`Chunk::addConstant`), so 300
  copies of `x = x + 1;` need three slots; only DISTINCT values count,
  and each function has its own pool. Going over the limit is an
  ordinary compile error, "Too many constants in one function."
  (reported once per function, with the line of the first constant that
  did not fit). The limit used to be 256 (one operand byte); see
  "Design note: wide constant operands". Array and map literals have
  their own, smaller limit of 65535 elements or entries, so a map
  literal is now bounded by that, or by the pool when its keys and
  values are all distinct (two slots per entry).
- **Jumps are 16-bit** (65535 bytes), as are array and map literal
  sizes.
- **Garbage is collected in batches, not instantly.** Reference
  counting frees ordinary garbage at once, but a cycle stays in memory
  until the next collection: at least 10,000 object allocations later,
  or more for a big heap (see "Design note: garbage collection"). A
  collection pauses the script for time linear in the number of heap
  objects.
- **A reference the collector cannot see survives.** Anything that
  holds a `shared_ptr` without reporting it from `traceChildren()` makes
  its target look externally referenced, so a cycle through it is never
  freed. No such reference exists today (bound natives used to be the
  one case, and report their receiver now); a new heap type or a native
  that stores a value must follow the rule in the design note.
- **Imported modules are never freed before the VM itself** (the cache
  keeps them), and each one carries its own copy of the standard
  library, a few dozen natives, which is a small fixed cost per
  imported file.
- **Very deep nesting overflows the C++ stack.** Printing, and
  eventually freeing, a nested array or map recurses once per level.
  Measured for arrays on an optimized build with an 8 MB stack: printing
  worked at 10,000 levels and crashed at 50,000; building and freeing
  worked at 100,000 and crashed at 400,000. Windows' default 1 MB stack
  allows roughly an eighth as much. The tree-walker has the same limit.

## Known differences from the tree-walker

For valid, deterministic programs the two engines print the same output;
the tests compare them line by line. What still differs:

- **Errors found at compile time instead of run time:** misuse of
  `this` or `super`, `class A < A`, and reading a local in its own
  initializer. The last is the one place the VM rejects a program the
  tree-walker accepts: in `{ var a = a; }` the tree-walker reads the
  OUTER `a`, while the VM reports "Cannot read a local variable in its
  own initializer.".
- **Compile-error reporting:** the VM prints "Compile error at ..."
  where the tree-walker prints "Error at ...", and the VM compiler has
  no panic mode, so one syntax error can be followed by several
  follow-on errors.
- **Errors the lexer reports** (an unknown escape sequence in a string, an
  unterminated string, a stray character) stop the VM from running the
  program: `Lexer::hadError()` is checked by `Compiler::compile`, which then
  returns no function. The tree-walker prints the same message and runs the
  rest of the file anyway, as it does for any syntax error. (Before this, the
  VM ran programs that only had a stray character, such as `print 1;@`.)
- **Line numbers for errors raised inside natives:** the VM reports the
  line of the call; the tree-walker prints `[line 0]`.
- **A failed `class A < x {}` in the REPL** leaves `A` defined as an
  empty class in the VM (the class is defined before the superclass is
  checked); the tree-walker leaves it undefined.
- **Where the tree-walker crashes or has undefined behavior,** the VM
  behaves: a container that holds itself prints `[...]` or `{...}`;
  NaN, infinite or enormous array indexes and slice bounds are range
  errors; `sort` checks the whole array first, so a failed sort leaves
  it untouched, and puts NaN last.
- **A syntax error in an imported file** is reported by the compiler (the
  same "Compile error at ..." lines) and then fails the import with
  "Could not compile imported file 'path'.", a runtime error at the
  `import` line.
- **`gc()` and `heapSize()` exist only in the VM.** A script that calls
  them fails in the tree-walker with "Undefined variable".
- **Limits the tree-walker does not have:** see "Known limits".

## How to run it

```bash
./mylang --vm script.mylang     # run a file through the VM
./mylang --vm                    # VM REPL
./mylang script.mylang           # tree-walking interpreter (full language, default)
```

The two engines are completely independent. The VM now covers the whole
language, including imports; the only differences are the ones listed
under "Known differences from the tree-walker".
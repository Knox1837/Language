# mylang — Language Specification

Status: living document. Update this whenever a language feature changes.
Currently covers: lexer, parser, tree-walking interpreter, functions &
closures, classes, inheritance, standard library, arrays, maps, imports.

## Overview

mylang is a dynamically-typed, interpreted scripting language with
C-style syntax (braces + semicolons) and Python-inspired function
declarations (`def`, no return-type annotations).

## Keywords

```
and       else      false     for       if        nil
or        print     return    true      var       while     def
class     this      super     import    as
```

## Data types

| Type   | Example        | Notes |
|--------|----------------|-------|
| number | `10`, `3.14`   | always stored as double-precision float internally |
| string | `"hello"`      | double-quoted only |
| bool   | `true`, `false`| |
| nil    | `nil`          | absence of a value; also the default for `var x;` with no initializer, and for a function with no `return` |
| function | `def f() {}` | first-class: can be assigned, passed, and returned |
| class  | `class C {}`   | first-class: calling a class constructs an instance |
| instance | (result of calling a class) | holds fields, created dynamically on first assignment |
| array  | `[1, 2, 3]`    | reference type — assigning or passing an array shares the same underlying data, like instances |
| map    | `{"key": val}` | reference type; **string keys only** (see design note below) |
| module | (result of `import ... as name`) | a namespace exposing the imported file's top-level `def`/`class`/`var` bindings via `.` |

## Truthiness

Only `nil` and `false` are falsey. Everything else — including the
number `0` and the empty string `""` — is truthy. (This differs from C,
where `0` is falsey.)

## Operators

| Category   | Operators | Notes |
|------------|-----------|-------|
| Arithmetic | `+ - * / %` | `/` always produces a float result; division/modulo by zero is a runtime error; `%` uses `fmod` semantics (result takes the sign of the dividend: `-7 % 3` is `-1`, not `2`) |
| `+` overload | `number + number`, `string + string` | mixing types throws a runtime error |
| Comparison | `> >= < <=` | numbers only |
| Equality   | `== !=` | works across any two values; different types are never equal |
| Logical    | `and or !` | `and`/`or` short-circuit and return one of their operand values (not necessarily a bool) |
| Assignment | `=` | itself an expression: `a = b = 5` works |
| Compound assignment | `+= -= *= /= %=` | works on variables, fields (`obj.f += 1`), and indices (`arr[i] += 1`, `map["k"] += 1`); the target is evaluated exactly once even if it has side effects (see design note below) |
| Indexing   | `arr[i]`, `arr[i] = x` | both are expressions; index-assignment returns the assigned value; also used for map access: `map["key"]` |

**Design note — how compound assignment avoids double evaluation:** `obj.field += 1` and `arr[i] += 1` don't desugar into two separate Get/Set (or Index/IndexSet) nodes — doing so would evaluate `obj`/`arr` (and `i`) *twice*, which would be wrong if that expression has side effects (e.g. `getObject().field += 1` calling `getObject()` twice). Instead, dedicated `CompoundSet`/`CompoundIndexSet` AST nodes evaluate the target exactly once and reuse that single result for both the read and the write. Plain variables (`x += 1`) still desugar directly to `x = x + 1`, since re-reading a variable by name has no side effects to duplicate.

## Grammar (EBNF-ish)

```
program     -> declaration* EOF
declaration -> importDecl | classDecl | funDecl | varDecl | statement
importDecl  -> "import" STRING "as" IDENTIFIER ";"
classDecl   -> "class" IDENTIFIER ( "<" IDENTIFIER )? "{" method* "}"
method      -> IDENTIFIER "(" parameters? ")" block
funDecl     -> "def" IDENTIFIER "(" parameters? ")" block
parameters  -> IDENTIFIER ( "," IDENTIFIER )*
varDecl     -> "var" IDENTIFIER ( "=" expression )? ";"
statement   -> exprStmt | printStmt | ifStmt | whileStmt | returnStmt | block
ifStmt      -> "if" "(" expression ")" statement ( "else" statement )?
whileStmt   -> "while" "(" expression ")" statement
forStmt     -> "for" "(" ( varDecl | exprStmt | ";" ) expression? ";" expression? ")" statement
returnStmt  -> "return" expression? ";"
block       -> "{" declaration* "}"
exprStmt    -> expression ";"
printStmt   -> "print" expression ";"

expression  -> assignment
assignment  -> ( call "." IDENTIFIER | call "[" expression "]" | IDENTIFIER ) "=" assignment
             | ( call "." IDENTIFIER | call "[" expression "]" | IDENTIFIER ) ( "+=" | "-=" | "*=" | "/=" | "%=" ) assignment
             | logicOr
logicOr     -> logicAnd ( "or" logicAnd )*
logicAnd    -> equality ( "and" equality )*
equality    -> comparison ( ( "!=" | "==" ) comparison )*
comparison  -> term ( ( ">" | ">=" | "<" | "<=" ) term )*
term        -> factor ( ( "-" | "+" ) factor )*
factor      -> unary ( ( "/" | "*" | "%" ) unary )*
unary       -> ( "!" | "-" ) unary | call
call        -> primary ( "(" arguments? ")" | "." IDENTIFIER | "[" expression "]" )*
arguments   -> expression ( "," expression )*
primary     -> NUMBER | STRING | "true" | "false" | "nil" | "this"
             | "(" expression ")" | IDENTIFIER | "super" "." IDENTIFIER
             | "[" ( expression ( "," expression )* )? "]"
             | "{" ( STRING ":" expression ( "," STRING ":" expression )* )? "}"
```

## Scoping

- Block scope: every `{ }` introduces a new scope; variables declared
  inside don't leak outside.
- Lexical (closure) scope for functions: a function captures the
  environment active at its **definition** site, not its call site.
  This means nested functions correctly retain access to outer
  variables even after the outer function has returned (see example
  below).

## For loops

C-style three-clause `for`, desugared by the parser into an equivalent `while` loop (no dedicated interpreter support needed):

```
for (var i = 0; i < 5; i = i + 1) {
    print i;
}
```

- All three clauses are optional: `for (;;) { ... }` is an infinite loop.
- The initializer clause may be a `var` declaration (scoped to the loop,
  including its body) or any expression statement (e.g. reusing an
  existing variable: `for (i = 0; ...; ...)`), or omitted entirely.
- The condition, if omitted, defaults to `true`.
- The increment runs after each iteration of the body, including when
  the body uses `return` to exit a surrounding function... but note
  there is currently no `break`/`continue`, so the only ways to leave a
  `for` loop early are the condition becoming false or a `return`.
- Because `for` desugars to `while`, the loop variable is a single
  mutable binding shared across iterations (not a fresh binding per
  iteration): a closure created inside the loop body and called after
  the loop sees the loop variable's *final* value, e.g.:
  ```
  var fns = [];
  for (var i = 0; i < 3; i = i + 1) {
      def make() { return i; }
      push(fns, make);
  }
  print fns[0](); // 3, not 0 -- all three closures share the same `i`
  ```

## Functions

```
def add(a, b) {
    return a + b;
}
print add(2, 3);   // 5
```

- No return type annotation — return type is whatever the returned
  value's runtime type is (or `nil` if no `return` is hit).
- Recursion works normally.
- Functions are values: they can be stored in variables, passed as
  arguments, and returned from other functions.

**Closures:**
```
def makeCounter() {
    var count = 0;
    def increment() {
        count = count + 1;
        return count;
    }
    return increment;
}
var counter = makeCounter();
print counter(); // 1
print counter(); // 2
```

## Classes

```
class Counter {
    init() {
        this.count = 0;
    }
    increment() {
        this.count = this.count + 1;
        return this.count;
    }
}
var c = Counter();
print c.increment(); // 1
print c.increment(); // 2
```

- Methods are declared **without** the `def` keyword inside a class body.
- `init()` is the constructor convention: called automatically when the
  class is invoked (`Counter()`), and always returns `this` regardless
  of what it explicitly returns (matches Lox/Python's `__init__`).
- Fields are dynamic — not declared up front, just created on first
  assignment (`this.count = 0`), same as Python's `self.x = ...`.
- Each instance has independent state; separate `Counter()` calls don't
  share fields.

## Inheritance

```
class Animal {
    init(name) {
        this.name = name;
    }
    speak() {
        print this.name + " makes a sound.";
    }
}

class Dog < Animal {
    speak() {
        print this.name + " barks.";
    }
    describe() {
        super.speak();   // calls Animal's speak()
        this.speak();    // calls Dog's own (overridden) speak()
    }
}

var d = Dog("Rex");
d.describe();
```

- `class Dog < Animal` declares `Dog` as a subclass of `Animal`.
- A subclass inherits all methods (including `init()`) it doesn't
  override.
- `super.method()` calls the named method from the superclass,
  bypassing any override in the current class.
- Multi-level inheritance works (`class Puppy < Dog` inherits from `Dog`,
  which inherits from `Animal`).
- The `< Superclass` expression must evaluate to an actual class;
  attempting to inherit from a non-class value is a runtime error
  ("Superclass must be a class.").

## Arrays

```
var arr = [1, 2, 3];
print arr[0];        // 1
arr[1] = 99;
print arr;           // [1, 99, 3]

var grid = [[1, 2], [3, 4]];
print grid[0][1];    // 2
```

- Array literals: `[expr, expr, ...]`, any mix of value types allowed.
- Indexing (`arr[i]`) and index-assignment (`arr[i] = x`) are both
  expressions; index-assignment returns the assigned value.
- Index must be a number; out-of-range or non-array indexing are
  runtime errors.
- **Reference semantics**: an array is a `shared_ptr` under the hood,
  same as class instances. Assigning an array to another variable, or
  passing it to a function, shares the same underlying data:
  ```
  var a = [1, 2, 3];
  var b = a;
  push(b, 4);
  print a; // [1, 2, 3, 4] -- a sees b's mutation
  ```
- Both **method-call syntax** (`arr.push(x)`) and the equivalent **free
  function** (`push(arr, x)`) work and are fully interchangeable — method
  syntax is just a more convenient way to call the same underlying
  operation. See the Standard Library section below for the full list.

## Maps

```
var person = {"name": "Knox", "age": 21};
print person["name"];    // Knox
person["age"] = 22;      // update existing key
person["city"] = "Kathmandu"; // insert new key
print person;            // {"age": 22, "city": Kathmandu, "name": Knox}
```

- Map literals: `{"key": expr, "key2": expr2, ...}` — **keys must be
  string literals** written directly in the source (not arbitrary
  expressions or bare identifiers), a deliberate simplification.
- **Design note — why string-only keys**: allowing arbitrary-type keys
  would require `Value` itself to be hashable/orderable, which is
  significant extra machinery for a feature most scripting languages'
  *simplest* map type doesn't need. String keys cover the common
  JSON-like-data use case cleanly.
- Reuses the same `[]` syntax as arrays: `map["key"]` reads, `map["key"]
  = x` writes. Unlike array indexing (fixed positions, out-of-range is
  an error), map index-assignment **always succeeds** — it inserts a new
  key if one doesn't exist yet, or overwrites an existing one.
- Reading a key that doesn't exist is a runtime error ("Undefined map
  key '...'").
- **Reference semantics**, same as arrays and instances — assigning or
  passing a map shares the underlying data.
- Iteration order (when printing, or via `keys()`/`values()`) is always
  **sorted by key**, not insertion order — deterministic and easy to test.
- Both **method-call syntax** (`map.hasKey(k)`) and the equivalent
  **free function** (`hasKey(map, k)`) work and are fully interchangeable.
- **Important distinction**: `.name` is only for calling a *method*
  (`map.hasKey(...)`) — it is NOT a way to read a stored value by key.
  `map.name` does not read the `"name"` entry; use `map["name"]` for that.
  This mirrors the same rule for arrays (`.push()` is a method call,
  `arr[0]` is how you read a value).

## Imports

```
// math_utils.mylang
def square(x) { return x * x; }
var PI_APPROX = 3.14;

// main.mylang
import "math_utils.mylang" as math;
print math.square(5);      // 25
print math.PI_APPROX;      // 3.14
```

- `import "path" as name` runs the imported file's top-level code once
  and binds `name` to a **module object** — accessing anything the file
  defined at its top level (`def`, `class`, `var`) goes through `.`,
  exactly like a class instance's fields/methods.
- **Caching**: importing the same resolved file path twice (from
  anywhere in the program, including from within another imported file)
  returns the **same** module object rather than re-running the file. A
  file's top-level code — including any `print` statements or other
  side effects it contains — only ever executes once per program run.
- **Isolation**: each imported file runs in its own fresh global scope.
  It cannot see or accidentally modify the importing script's variables,
  and the importing script can't see the imported file's variables
  except through the module object's namespace.
- **Circular imports** (file A imports B, which imports A) are detected
  and reported as a runtime error rather than infinite-looping.
- **Path resolution (first-pass design)**: relative import paths always
  resolve against the **top-level script's** directory — including
  `import` statements written inside an already-imported file. This is
  simpler than per-file relative resolution (how Node/Python do it) and
  correct for the common case of a project's `.mylang` files living in
  one flat directory. Per-file-relative resolution is a reasonable
  future expansion, not yet implemented.
- In the REPL (no script file), imports resolve relative to the current
  working directory.

## Standard library

A set of native (C++-implemented) built-in functions are available
globally in every script — no import needed.

**Math**

| Function | Signature | Notes |
|---|---|---|
| `clock()` | () → number | CPU time in seconds since program start |
| `abs(x)` | number → number | |
| `sqrt(x)` | number → number | error if `x < 0` |
| `pow(x, y)` | number, number → number | |
| `floor(x)` / `ceil(x)` / `round(x)` | number → number | |
| `min(a, b)` / `max(a, b)` | number, number → number | |
| `sin(x)` / `cos(x)` / `tan(x)` | number → number | radians |
| `log(x)` | number → number | natural log; error if `x <= 0` |
| `log10(x)` | number → number | base-10 log; error if `x <= 0` |
| `PI`, `E` | constants, not functions | used as `PI`, not `PI()` |
| `random()` | () → number | returns a float in `[0, 1)` |
| `randomInt(min, max)` | number, number → number | inclusive integer range |
| `setSeed(n)` | number → nil | reseeds the shared RNG for reproducible `random()`/`randomInt()` sequences |

**Strings**

| Function | Signature | Notes |
|---|---|---|
| `len(s)` | string → number | string length |
| `upper(s)` / `lower(s)` | string → string | |
| `substring(s, start, end)` | string, number, number → string | end-exclusive, like Python's `s[start:end]` |
| `charAt(s, i)` | string, number → string | single character as a 1-length string |
| `find(s, sub)` | string, string → number | index of first occurrence, or `-1`; named `find` (not `indexOf`) to avoid colliding with the array function of the same concept |
| `startsWith(s, prefix)` / `endsWith(s, suffix)` | string, string → bool | |
| `trim(s)` | string → string | strips leading/trailing whitespace |
| `replace(s, old, new)` | string, string, string → string | replaces **all** occurrences |
| `split(s, delimiter)` | string, string → array | delimiter must be non-empty |
| `join(arr, delimiter)` | array, string → string | inverse of `split`; elements stringified with the same rules as `str()` |
| `toNumber(s)` | string → number | parses a string to a number; error on invalid input — currently the only string→number conversion path |
| `str(x)` | any → string | converts any value to its string form |

**Arrays**

| Function | Signature | Notes |
|---|---|---|
| `push(arr, x)` | array, any → number | appends in place, returns new length |
| `pop(arr)` | array → any | removes & returns the last element; error if empty |
| `length(arr)` | array → number | number of elements (separate from `len`, which is string-only); also accepts maps — see Maps section below |
| `contains(arr, x)` | array, any → bool | true if `x` appears anywhere in the array |
| `indexOf(arr, x)` | array, any → number | first matching index, or `-1` |
| `sort(arr)` | array → array | ascending, **in place**; numbers-only or strings-only (mixed types error) |
| `reverse(arr)` | array → array | in place |
| `slice(arr, start, end)` | array, number, number → array | returns a **new** array, end-exclusive; does not mutate the original |
| `binarySearch(arr, x)` | array, any → number | O(log n); **assumes `arr` is already sorted ascending** — call `sort()` first |

**Maps**

| Function | Signature | Notes |
|---|---|---|
| `keys(map)` | map → array | all keys, sorted; returns a new array (a snapshot, not a live view) |
| `values(map)` | map → array | all values, in the same sorted-by-key order as `keys()` |
| `hasKey(map, k)` | map, string → bool | |
| `remove(map, k)` | map, string → bool | deletes the entry in place; returns whether it existed |
| `length(map)` | map → number | shared with arrays — see above |

**I/O**

| Function | Signature | Notes |
|---|---|---|
| `input()` | () → string | reads one line from stdin; returns `""` on EOF (no prompt argument — print your own prompt first) |

**Type checks**

| Function | Signature | Notes |
|---|---|---|
| `isNumber(x)` / `isString(x)` / `isBool(x)` / `isArray(x)` / `isMap(x)` / `isNil(x)` | any → bool | |
| `isFunction(x)` | any → bool | true for both user-defined functions AND classes (a class is callable — calling it constructs an instance) |

Native functions raise the same `RuntimeError` mechanism as the rest of
the interpreter, but since they aren't tied to a specific AST node, their
error messages report line `0` rather than the calling line.

## Error handling

- **Parse errors** (e.g. `Expect ';' after value.`) are reported with
  a line number; the parser attempts to recover (`synchronize()`) and
  continue reporting further errors instead of stopping at the first one.
- **Runtime errors** (undefined variable, type mismatch, division by
  zero, wrong argument count, calling a non-function, accessing a
  property on a non-instance, undefined property, invalid superclass,
  array index out of range/wrong type) are reported with a line number
  and halt execution of that script/REPL line.

## Not yet implemented

- Non-string map keys
- Per-file-relative import path resolution (currently all imports resolve against the top-level script's directory — see Imports section above)
- Bytecode VM (current implementation is a tree-walking interpreter)
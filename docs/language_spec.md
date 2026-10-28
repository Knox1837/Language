# mylang - Language Specification

Status: living document. Update this whenever a language feature changes.
Currently covers: lexer, parser, functions & closures, classes,
inheritance, standard library, arrays, maps, imports. Everything here is
implemented by both execution engines (the tree-walking interpreter and the
bytecode VM, `--vm`); the few places where the VM differs are listed in
`docs/vm_notes.md`.

## Overview

mylang is a dynamically-typed, interpreted scripting language with
C-style syntax (braces + semicolons) and Python-inspired function
declarations (`def`, no return-type annotations).

## Keywords

```
and       else      false     for       if        nil
or        print     return    true      var       while     def
class     this      super     import    as        break     continue
in
```

`in` is reserved (it is the keyword of the for-in loop), so it can no longer be
used as a variable or function name.

## Data types

| Type   | Example        | Notes |
|--------|----------------|-------|
| number | `10`, `3.14`   | always stored as double-precision float internally |
| string | `"hello"`      | double-quoted only; a backslash starts an escape sequence (see "String escapes" below); may span several lines |
| bool   | `true`, `false`| |
| nil    | `nil`          | absence of a value; also the default for `var x;` with no initializer, and for a function with no `return` |
| function | `def f() {}` | first-class: can be assigned, passed, and returned |
| class  | `class C {}`   | first-class: calling a class constructs an instance |
| instance | (result of calling a class) | holds fields, created dynamically on first assignment |
| array  | `[1, 2, 3]`    | reference type — assigning or passing an array shares the same underlying data, like instances |
| map    | `{"key": val}` | reference type; **string keys only** (see design note below) |
| module | (result of `import ... as name`) | a namespace exposing the imported file's top-level `def`/`class`/`var` bindings via `.` |

## String escapes

Inside a string literal a backslash starts an escape sequence, which stands for
one character:

| Written | Character |
|---------|-----------|
| `\n`    | newline |
| `\t`    | tab |
| `\r`    | carriage return |
| `\\`    | a single backslash |
| `\"`    | a double quote (so it does not end the string) |

```
print "name:\tKnox\nsaid \"hi\"";   // two lines, a tab, and quotes
print len("a\nb");                  // 3: each escape is one character
print "C:\\temp";                   // C:\temp
print "\\n";                        // \n -- a backslash and an n, not a newline
```

- **Any other backslash sequence is an error:** `"bad \q"` reports
  `Unknown escape sequence '\q' in string.` The VM refuses to run the program;
  the tree-walking interpreter reports the error, keeps the two characters as
  written, and carries on (as it does for every syntax error).
- A string that ends in a backslash must write it as `\\` (`"dir\\"`), because
  `\"` is an escaped quote and would leave the string unterminated.
- A string literal may also contain real line breaks; they are part of the value.
- There are no `\0`, `\xNN`, or Unicode escapes. Strings are byte strings, so
  UTF-8 text typed directly in a string is stored and printed as it is, and
  `len` counts bytes (`len("é")` is 2).
- Windows paths: write `\\` or use `/`, which Windows accepts too. On Windows
  `"sub/lib.mylang"` and `"sub\\lib.mylang"` both name the same file, while
  `"sub\lib.mylang"` is an unknown-escape error.

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
| Indexing   | `arr[i]`, `arr[i] = x`, `s[i]` | both array forms are expressions; index-assignment returns the assigned value; also used for map access: `map["key"]`. `s[i]` reads one byte of a string as a 1-character string (same as `charAt(s, i)`); strings are immutable, so `s[i] = x` is a runtime error |

**Design note — how compound assignment avoids double evaluation:** `obj.field += 1` and `arr[i] += 1` don't desugar into two separate Get/Set (or Index/IndexSet) nodes — doing so would evaluate `obj`/`arr` (and `i`) *twice*, which would be wrong if that expression has side effects (e.g. `getObject().field += 1` calling `getObject()` twice). Instead, dedicated `CompoundSet`/`CompoundIndexSet` AST nodes evaluate the target exactly once and reuse that single result for both the read and the write. Plain variables (`x += 1`) still desugar directly to `x = x + 1`, since re-reading a variable by name has no side effects to duplicate.

## Grammar (EBNF-ish)

```
program     -> declaration* EOF
declaration -> importDecl | classDecl | funDecl | varDecl | statement
importDecl  -> "import" STRING "as" IDENTIFIER ";"
classDecl   -> "class" IDENTIFIER ( "<" IDENTIFIER )? "{" method* "}"
method      -> IDENTIFIER "(" parameters? ")" block
funDecl     -> "def" IDENTIFIER "(" parameters? ")" block
funExpr     -> "def" "(" parameters? ")" block
parameters  -> IDENTIFIER ( "," IDENTIFIER )*
varDecl     -> "var" IDENTIFIER ( "=" expression )? ";"
statement   -> exprStmt | printStmt | ifStmt | whileStmt | forStmt | returnStmt
             | breakStmt | continueStmt | block
ifStmt      -> "if" "(" expression ")" statement ( "else" statement )?
whileStmt   -> "while" "(" expression ")" statement
forStmt     -> "for" "(" ( varDecl | exprStmt | ";" ) expression? ";" expression? ")" statement
             | "for" "(" "var" IDENTIFIER "in" expression ")" statement
returnStmt  -> "return" expression? ";"
breakStmt   -> "break" ";"
continueStmt -> "continue" ";"
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
             | "(" expression ")" | IDENTIFIER | "super" "." IDENTIFIER | funExpr
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

C-style three-clause `for`, treated as a `while` loop with an increment clause (the parser builds the same loop node for both, so the interpreter has no separate `for` support):

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
- The increment runs after each iteration of the body, including an
  iteration the body ended with `continue` (see "Break and continue"
  below). A loop can be left early with `break`, with a `return`, or
  by its condition becoming false.
- Because `for` is a `while` loop underneath, the loop variable is a single
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

## For-in loops

`for (var x in collection) body` runs the body once per element:

```
for (var n in [10, 20, 30]) {
    print n;                        // 10 20 30
}

var ages = {"zed": 30, "amy": 25};
for (var name in ages) {
    print name + "=" + str(ages[name]);   // amy=25, then zed=30
}
```

- The collection must be an **array** (the loop variable takes each element in
  turn) or a **map** (it takes each *key*, in the map's sorted key order; look
  the value up with `m[key]`). Anything else, a string included, is a runtime
  error: `Can only iterate over arrays and maps.` To walk a string use a
  three-clause `for` with `s[i]`.
- The `var` is required: `for (x in xs)` is a syntax error (`Expect 'var' before
  the loop variable in a for-in loop.`).
- The collection expression is evaluated **once**, before the loop starts.
- Every iteration gets its **own** loop variable, unlike the three-clause `for`
  (see above): closures created in the body each keep that iteration's value.
  ```
  var fns = [];
  for (var v in [10, 20, 30]) { push(fns, def() { return v; }); }
  print fns[0]();   // 10, not 30
  ```
- The variable is a copy of the element. Assigning to it does not change the
  array.
- An array is walked by position, and its length is checked again on every
  iteration. Elements the body appends are visited, elements it removes are not,
  and the loop can never run past the end. A map is walked over a snapshot of its
  keys, so adding or removing entries in the body does not disturb the loop (keys
  added during the loop are not visited; a key removed during the loop is still
  visited, and `m[key]` on it then fails as it would anywhere else).
- `break`, `continue` and `return` work as in any other loop. The loop variable's
  scope is the loop: it is not visible afterwards, and it may shadow an outer
  variable of the same name.

## Break and continue

`break;` leaves the innermost enclosing `while`, `for` or for-in loop immediately.
`continue;` skips the rest of the current iteration of the innermost enclosing
loop and starts the next one:

```
var i = 0;
while (true) {
    i = i + 1;
    if (i == 3) { continue; }   // skip printing 3
    if (i > 5) { break; }       // stop after 5
    print i;                    // 1 2 4 5
}
```

- They apply to the **innermost** loop only. There are no labels, so leaving
  two loops at once takes a flag variable or a function with `return`.
- In a `for` loop, `continue` still runs the **increment** clause before the
  condition is checked again, so `for (var i = 0; i < 10; i = i + 1)` with a
  `continue` in its body cannot get stuck. (`break` does not run the increment.)
  With no increment clause, `continue` goes straight to the condition.
- They work from any depth inside the loop body: through nested blocks and
  `if`/`else` branches. Variables declared in the body go out of scope as the
  loop is left, and a closure that captured one of them keeps its own copy of
  it, exactly as when the body ends normally.
- A loop does not extend into a function declared inside it: `break` or
  `continue` inside a `def` (or a method) that is itself inside a loop, but not
  inside a loop of its own, is an error.
- Using either outside a loop is a syntax error: `Can't use 'break' outside of
  a loop.` (or `'continue'`). The VM refuses to run the program; the
  tree-walking interpreter reports the error and runs the rest, as it does for
  any syntax error.

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

## Function expressions

`def` with no name, followed straight away by a parameter list, is an
*expression*: an anonymous function that is a value like any other.

```
var double = def(x) { return x * 2; };
print double(21);                         // 42
print def(a, b) { return a + b; }(3, 4);  // 7 -- called right where it is written
print double;                             // <fn>   (a named function prints as <fn name>)
```

- It is a closure, exactly like a named function: it captures the variables
  of the scopes around it, and each call of the function that creates it gets
  its own copy of them.
- Being a value, it can be stored in a variable, an array, a map or an
  instance field, passed to a function, and returned from one.
  `makeAdder(5)(10)` and `table["neg"](5)` call the function they produce.
- It cannot be a statement on its own: a statement that starts with `def` is
  a function *declaration* and needs a name, so `def(x) { ... };` is the error
  `Expect function name.` Store it in a variable or pass it somewhere.
- It has no name to call itself by. Recurse through the variable it is stored
  in: `var fact = def(n) { if (n <= 1) { return 1; } return n * fact(n - 1); };`
  works at the top level and inside a function.
- `break` and `continue` inside one belong to the loops inside it, never to a
  loop that surrounds the function expression.
- `map`, `filter`, `reduce` and `sort` with a comparator take functions like
  this; see the Arrays section.

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

- The path is an ordinary string literal, so backslashes in it are escape
  sequences: write `/` (works on every platform) or `\\`, never a single `\`.
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
- **Errors inside an imported file** are ordinary runtime errors: the
  error is reported with the line it happened on (inside the imported
  file) and the whole program stops, exactly as if it had happened in
  the importing script. A file that fails part-way is not cached, so
  importing it again (for example on a later REPL line) runs it again.
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
| `sort(arr)` | array → array | ascending, **in place**, stable (Timsort); numbers-only or strings-only (mixed types error, and nothing is moved); a NaN sorts after every other number; an array of fewer than two elements is accepted whatever it holds |
| `sort(arr, cmp)` | array, function → array | sorts **in place** by a comparator (also Timsort, stable). See "Functions that take a function" below |
| `reverse(arr)` | array → array | in place |
| `slice(arr, start, end)` | array, number, number → array | returns a **new** array, end-exclusive; does not mutate the original |
| `binarySearch(arr, x)` | array, any → number | O(log n); **assumes `arr` is already sorted ascending** — call `sort()` first |
| `map(arr, f)` | array, function → array | a **new** array of `f(element)` for every element |
| `filter(arr, f)` | array, function → array | a **new** array of the elements for which `f(element)` is truthy |
| `reduce(arr, f, initial)` | array, function, any → any | `acc = initial`, then `acc = f(acc, element)` for each element, left to right; returns `acc` (an empty array gives `initial`) |

**Functions that take a function** (`map`, `filter`, `reduce`, `sort` with a
comparator). Each also works as a method: `arr.map(f)`, `arr.filter(f)`,
`arr.reduce(f, initial)`, `arr.sort(cmp)`, so calls chain:
`nums.filter(def(x) { return x > 2; }).map(def(x) { return x * 10; })`.

- `f` can be any callable value: a `def` function, a function expression, a
  built-in such as `str` or `len`, a class (its constructor is called), or a
  bound method (`map(xs, obj.scale)`).
- `map` and `filter` call it with one argument (the element); `reduce` with two
  (the accumulator, then the element); a `sort` comparator with two elements.
  A function that takes a different number of parameters is the usual error:
  `Expected 2 arguments but got 1.`
- `filter` uses the language's truthiness (see Truthiness): only `false` and
  `nil` drop an element, so a callback that returns `0` keeps it.
- A **comparator** `cmp(a, b)` returns a number: negative if `a` belongs before
  `b`, positive if `a` belongs after `b`, zero if they are equal. Equal elements
  keep their original order (the sort is stable). Returning anything but a
  number is the error `Comparator must return a number.` A comparator that
  makes no sense (always `1`, say) gives some order, never a crash.
- They work on a **snapshot** of the array taken when they start, so a
  callback that pushes to or pops from the array being processed does not
  disturb the loop. A sort replaces the array's contents only when it
  finishes, so a comparator that fails leaves the array as it was.
- An error inside a callback stops the program and is reported once, with the
  line of the callback.
- Both forms of `sort` use Timsort: it is stable, it needs only about `n` comparisons for an array that is already sorted or reversed, and at most about `n log n` for any array. Calling `cmp` fewer times matters when `cmp` is a script function.

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
the interpreter, but since they aren't tied to a specific AST node, the
tree-walking interpreter reports their errors at line `0` rather than the
calling line (the VM reports the line of the call).

**VM-only functions:** `gc()` runs the garbage collector and returns how many
objects it freed, and `heapSize()` returns how many heap objects exist. They
exist so scripts and tests can observe the collector; the tree-walking
interpreter does not define them.

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

Each of these was checked against both engines. A script that uses one gets
a parse error, or "Undefined variable" for the names that are not keywords.

- **Raising and handling errors from a script:** no `try`/`catch`, no
  `error(...)` function. Every runtime error stops the program.
- **File I/O:** no way to read or write a file from a script (`input()` reads
  standard input only).
- **Non-string map keys** (map literals and `m[k]` accept strings only).
- **Per-file-relative import path resolution** (currently all imports resolve
  against the top-level script's directory — see Imports section above).
- **Operators and syntax:** no `?:` conditional, no `++`/`--`, and no block
  comments (`/* ... */`; `//` line comments work). The for-in loop walks arrays
  and maps only, not strings.
- **Number formatting:** `print` shows whole numbers as integers and any other
  number with six decimal places (`3.14` prints as `3.140000`).
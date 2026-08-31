# mylang — Language Specification

Status: Development

## Overview

mylang is a dynamically-typed, interpreted scripting language with
C-style syntax (braces + semicolons) and Python-inspired function
declarations (`def`, no return-type annotations).

## Keywords

```
and       else      false     for       if        nil
or        print     return    true      var       while     def
```

## Data types

| Type   | Example        | Notes |
|--------|----------------|-------|
| number | `10`, `3.14`   | always stored as double-precision float internally |
| string | `"hello"`      | double-quoted only |
| bool   | `true`, `false`| |
| nil    | `nil`          | absence of a value; also the default for `var x;` with no initializer, and for a function with no `return` |
| function | `def f() {}` | first-class: can be assigned, passed, and returned |

## Truthiness

Only `nil` and `false` are falsey. Everything else — including the
number `0` and the empty string `""` — is truthy. (This differs from C,
where `0` is falsey.)

## Operators

| Category   | Operators | Notes |
|------------|-----------|-------|
| Arithmetic | `+ - * /` | `/` always produces a float result; division by zero is a runtime error |
| `+` overload | `number + number`, `string + string` | mixing types throws a runtime error |
| Comparison | `> >= < <=` | numbers only |
| Equality   | `== !=` | works across any two values; different types are never equal |
| Logical    | `and or !` | `and`/`or` short-circuit and return one of their operand values (not necessarily a bool) |
| Assignment | `=` | itself an expression: `a = b = 5` works |

No `%` (modulo) operator yet.

## Grammar (EBNF-ish)

```
program     -> declaration* EOF
declaration -> funDecl | varDecl | statement
funDecl     -> "def" IDENTIFIER "(" parameters? ")" block
parameters  -> IDENTIFIER ( "," IDENTIFIER )*
varDecl     -> "var" IDENTIFIER ( "=" expression )? ";"
statement   -> exprStmt | printStmt | ifStmt | whileStmt | returnStmt | block
ifStmt      -> "if" "(" expression ")" statement ( "else" statement )?
whileStmt   -> "while" "(" expression ")" statement
returnStmt  -> "return" expression? ";"
block       -> "{" declaration* "}"
exprStmt    -> expression ";"
printStmt   -> "print" expression ";"

expression  -> assignment
assignment  -> IDENTIFIER "=" assignment | logicOr
logicOr     -> logicAnd ( "or" logicAnd )*
logicAnd    -> equality ( "and" equality )*
equality    -> comparison ( ( "!=" | "==" ) comparison )*
comparison  -> term ( ( ">" | ">=" | "<" | "<=" ) term )*
term        -> factor ( ( "-" | "+" ) factor )*
factor      -> unary ( ( "/" | "*" ) unary )*
unary       -> ( "!" | "-" ) unary | call
call        -> primary ( "(" arguments? ")" )*
arguments   -> expression ( "," expression )*
primary     -> NUMBER | STRING | "true" | "false" | "nil"
             | "(" expression ")" | IDENTIFIER
```

## Scoping

- Block scope: every `{ }` introduces a new scope; variables declared
  inside don't leak outside.
- Lexical (closure) scope for functions: a function captures the
  environment active at its **definition** site, not its call site.
  This means nested functions correctly retain access to outer
  variables even after the outer function has returned (see example
  below).

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

## Error handling

- **Parse errors** (e.g. `Expect ';' after value.`) are reported with
  a line number; the parser attempts to recover (`synchronize()`) and
  continue reporting further errors instead of stopping at the first one.
- **Runtime errors** (undefined variable, type mismatch, division by
  zero, wrong argument count, calling a non-function) are reported with
  a line number and halt execution of that script/REPL line.

## Not yet implemented

- Classes / objects / inheritance
- `%` modulo, compound assignment (`+=` etc.)
- Arrays / lists, maps
- A standard library (no built-in functions at all currently — not even a way to get the time or read input)
- Bytecode VM (current implementation is a tree-walking interpreter)
# mylang

## Build

**With g++ directly:**
```bash
g++ -std=c++17 -Isrc src/main.cpp src/lexer/lexer.cpp src/parser/parser.cpp src/interpreter/environment.cpp src/interpreter/interpreter.cpp src/interpreter/user_function.cpp src/interpreter/value.cpp -o mylang
```

**With CMake:**
```bash
cmake -B build
cmake --build build
```

## Run

**Run a script file:**
```bash
./mylang path/to/script.mylang
```

**Start the REPL (no file argument):**
```bash
./mylang
```

## Docs

See `docs/language-spec.md` for the language grammar/semantics
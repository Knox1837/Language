// vm_function.h: a compiled function: its own independent Chunk (a function's body is compiled into a separate bytecode array from whatever's calling it)
#pragma once
#include <string>
#include "chunk.h"

class VMFunction {
public:
    Chunk chunk;
    int arity = 0;      // the number of parameters (the MOST arguments a call may pass)
    bool hasRest = false; // the last parameter is a rest parameter (def f(a, ...rest)): `arity` counts only the OTHER parameters, and the extra arguments arrive in one more local, an array, right after them
    int minArity = 0;   // how many of them have no default (the FEWEST arguments a call may pass)
    int upvalueCount = 0;
    std::string name;
};
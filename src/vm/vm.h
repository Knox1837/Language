// vm.h: the bytecode interpreter loop itself. Reads one instruction at a time from a Chunk and acts on it using a small value stack for intermediate results
// no tree-walking, no AST, no recursion, no call stack, no closures, no environments: just a flat array of bytes and a stack of values.
#pragma once
#include <vector>
#include <string>
#include "chunk.h"

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR,
};

class VM {
public:
    // Compiles and runs `source` in one call: convenience entry point matching how main.cpp invokes the tree-walking interpreter.
    InterpretResult interpret(const std::string& source);

private:
    Chunk chunk;
    size_t ip = 0; // instruction pointer: index into chunk.code of the NEXT byte to read
    std::vector<VMValue> stack;

    InterpretResult run();

    uint8_t readByte();
    VMValue readConstant();

    void push(VMValue value);
    VMValue pop();

    void runtimeError(const std::string& message);
};
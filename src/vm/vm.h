// vm.h: the bytecode interpreter loop itself. Reads one instruction at a time from a Chunk and acts on it using a small value stack for intermediate results
// no tree-walking, no AST, no recursion, no call stack, no closures, no environments: just a flat array of bytes and a stack of values.
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
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

    // Global variables, keyed by name. Flat (no scope chain) since this increment only covers globals
    std::unordered_map<std::string, VMValue> globals;

    InterpretResult run();

    uint8_t readByte();
    VMValue readConstant();

    void push(VMValue value);
    VMValue pop();
    const VMValue& peekStack(int distanceFromTop) const;

    // Type-checked arithmetic helper shared by OP_ADD/SUBTRACT/etc.
    // returns false (and reports the error) if either operand isn't a number, so run() can bail out with RUNTIME_ERROR cleanly.
    bool requireNumbers(const VMValue& a, const VMValue& b, const char* opName);

    void runtimeError(const std::string& message);
};
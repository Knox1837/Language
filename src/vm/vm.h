// vm.h: the bytecode interpreter loop itself. Reads one instruction at a time from a Chunk and acts on it using a small value stack for intermediate results
// no tree-walking, no AST, no recursion, no call stack, no closures, no environments: just a flat array of bytes and a stack of values.
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include "chunk.h"
#include "vm_function.h"
#include "vm_closure.h"
#include "vm_upvalue.h"

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR,
};

struct CallFrame {
    std::shared_ptr<VMClosure> closure;
    size_t ip = 0;          // instruction pointer, an index into closure->function->chunk.code
    size_t stackBase = 0;   // index into VM::stack where THIS call's slot 0 begins
};

class VM {
public:
    // Compiles and runs `source` in one call: convenience entry point matching how main.cpp invokes the tree-walking interpreter.
    InterpretResult interpret(const std::string& source);

private:
    std::vector<VMValue> stack;
    std::vector<CallFrame> frames;

    // Every upvalue currently referring to a stack slot that's still live 
    std::vector<std::shared_ptr<VMUpvalue>> openUpvalues;

    // Global variables, keyed by name. Flat (no scope chain) since globals were never given real lexical scoping — only locals were.
    std::unordered_map<std::string, VMValue> globals;

    InterpretResult run();

    CallFrame& currentFrame() { return frames.back(); }

    uint8_t readByte();
    uint16_t readShort(); // reads a 2-byte big-endian operand (jump offsets)
    VMValue readConstant();

    void push(VMValue value);
    VMValue pop();
    const VMValue& peekStack(int distanceFromTop) const;

    // Attempts to call `callee` with `argCount` arguments already sittingon top of the stack (with `callee` itself just below them). 
    // pushes a new CallFrame on success; returns false (and reports a runtime error) if `callee` isn't callable or the argument count is wrong.
    bool callValue(const VMValue& callee, int argCount);
    bool call(std::shared_ptr<VMClosure> closure, int argCount);

    // Returns the (possibly newly-created) VMUpvalue for the given ABSOLUTE stack index.
    // If one already exists for that exact slot (because another closure already captured it), the SAME object is returned
    std::shared_ptr<VMUpvalue> captureUpvalue(size_t stackIndex);

    // Closes every still-open upvalue whose stackIndex is >= `fromIndex`. Called whenever the stack slots at or above that point are aboutto be reused
    void closeUpvalues(size_t fromIndex);

    // Type-checked arithmetic helper shared by OP_ADD/SUBTRACT/etc.
    // returns false (and reports the error) if either operand isn't a number, so run() can bail out with RUNTIME_ERROR cleanly.
    bool requireNumbers(const VMValue& a, const VMValue& b, const char* opName);

    // Equality across any two VMValues, including different alternative types
    bool areVMEqual(const VMValue& a, const VMValue& b);

    void runtimeError(const std::string& message);
};
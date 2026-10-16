// vm.h: the bytecode interpreter loop itself. Reads one instruction at a time from a Chunk and acts on it using a small value stack for intermediate results
// no tree-walking, no AST, no recursion, no call stack, no closures, no environments: just a flat array of bytes and a stack of values.
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include "chunk.h"
#include "vm_function.h"
#include "vm_closure.h"
#include "vm_upvalue.h"
#include "vm_module.h"

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR,
};

struct CallFrame {
    std::shared_ptr<VMClosure> closure;
    size_t ip = 0;          // instruction pointer, an index into closure->function->chunk.code
    size_t stackBase = 0;   // index into VM::stack where THIS call's slot 0 begins
    std::shared_ptr<VMModule> importedModule; // set only on the frame that runs an imported file's top-level code: when it returns, the module (not nil) is the result, and it is cached
};

class VM {
public:
    VM(); // defines the native functions (see vm_stdlib.cpp) as globals
    ~VM(); // drops every root and runs a last collection, so even reference cycles are freed when the VM goes away
    // Compiles and runs `source` in one call: convenience entry point matching how main.cpp invokes the tree-walking interpreter.
    InterpretResult interpret(const std::string& source);

    // Relative import paths resolve against this directory (the top-level script's directory, or "." in the REPL). Without it, `import` is a runtime error, as in the tree-walker.
    void setModuleBaseDir(const std::string& dir);

private:
    std::vector<VMValue> stack;
    std::vector<CallFrame> frames;

    // Every upvalue currently referring to a stack slot that's still live 
    std::vector<std::shared_ptr<VMUpvalue>> openUpvalues;

    // The main script's module; its globals are the "global variables" of the top-level script (flat, keyed by name: globals never had real lexical scoping, only locals did).
    // It outlives every REPL line. Imported files get their own VMModule, found through moduleCache.
    std::shared_ptr<VMModule> mainModule;

    std::string moduleBaseDir;
    bool hasModuleBaseDir = false;
    std::unordered_map<std::string, std::shared_ptr<VMModule>> moduleCache; // resolved path -> module, filled when a file finishes loading
    std::unordered_set<std::string> currentlyLoading;                        // resolved paths whose top-level code is running now (circular-import detection)
    std::string resolveModulePath(const std::string& path) const;

    // The globals table of the code that is running right now: the module its closure was created in.
    std::unordered_map<std::string, VMValue>& currentGlobals() { return currentFrame().closure->module->globals; }

    InterpretResult run();

    CallFrame& currentFrame() { return frames.back(); }

    uint8_t readByte();
    uint16_t readShort(); // reads a 2-byte big-endian operand (jump offsets)
    VMValue readConstant(); // reads a 1-byte constant index, or a 2-byte one if the previous instruction was OP_WIDE
    bool wideOperand = false; // set by OP_WIDE, consumed by the next readConstant()

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
    bool requireNumbers(const VMValue& a, const VMValue& b);

    // Equality across any two VMValues, including different alternative types
    bool areVMEqual(const VMValue& a, const VMValue& b);

    void runtimeError(const std::string& message);
};
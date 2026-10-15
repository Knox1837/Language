// vm.cpp: the actual bytecode execution loop. 
#include "vm.h"
#include "compiler.h"
#include "gc.h"
#include "vm_array.h"
#include "vm_array_lib.h"
#include "vm_class.h"
#include "vm_map.h"
#include "vm_map_lib.h"
#include "vm_native.h"
#include "vm_stdlib.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

VM::VM() {
    mainModule = std::make_shared<VMModule>();
    registerVMStdlib(mainModule->globals);
}

VM::~VM() {
    // Let go of everything the VM itself holds. What is left is held only by other heap objects, and one collection frees all of it, cycles included.
    frames.clear();
    stack.clear();
    openUpvalues.clear();
    mainModule->globals.clear();
    moduleCache.clear();
    GC::collect();
}

void VM::setModuleBaseDir(const std::string& dir) {
    moduleBaseDir = dir;
    hasModuleBaseDir = true;
}

// Same rule as the tree-walker's ModuleLoader: relative paths resolve against the base directory, and weakly_canonical normalizes "." and ".." so the cache key is the same however the path was written.
std::string VM::resolveModulePath(const std::string& path) const {
    namespace fs = std::filesystem;
    fs::path p(path);
    fs::path resolved = p.is_absolute() ? p : fs::path(moduleBaseDir) / p;
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(resolved, ec);
    return ec ? resolved.string() : canonical.string();
}

InterpretResult VM::interpret(const std::string& source) {
    stack.clear();
    frames.clear();
    openUpvalues.clear();
    currentlyLoading.clear(); // a runtime error inside an import on an earlier REPL line must not leave that file "still loading"

    Compiler compiler;
    std::shared_ptr<VMFunction> script = compiler.compile(source);
    if (!script) {
        return InterpretResult::COMPILE_ERROR;
    }

    // The top-level script is wrapped in a VMClosure too (with zero upvalues , nothing encloses it), built directly here rather than via OP_CLOSURE bytecode, since there's no OUTER chunk for such an instruction to live in. 
    // This keeps it on the same CallFrame footing as any real function from this point on.
    auto scriptClosure = std::make_shared<VMClosure>();
    scriptClosure->function = script;
    scriptClosure->module = mainModule.get();

    push(VMValue{scriptClosure}); // occupies slot 0, same convention every real call uses
    frames.push_back(CallFrame{scriptClosure, 0, 0, nullptr});

    return run();
}

uint8_t VM::readByte() {
    return currentFrame().closure->function->chunk.code[currentFrame().ip++];
}

uint16_t VM::readShort() {
    uint8_t high = readByte();
    uint8_t low = readByte();
    return (static_cast<uint16_t>(high) << 8) | low;
}

VMValue VM::readConstant() {
    size_t index = wideOperand ? readShort() : readByte();
    wideOperand = false;
    return currentFrame().closure->function->chunk.constants[index];
}

void VM::push(VMValue value) {
    stack.push_back(std::move(value));
}

VMValue VM::pop() {
    VMValue value = std::move(stack.back());
    stack.pop_back();
    return value;
}

const VMValue& VM::peekStack(int distanceFromTop) const {
    return stack[stack.size() - 1 - distanceFromTop];
}

bool VM::requireNumbers(const VMValue& a, const VMValue& b, const char* opName) {
    if (isVMNumber(a) && isVMNumber(b)) return true;
    std::ostringstream msg;
    msg << "Operands to '" << opName << "' must be numbers.";
    runtimeError(msg.str());
    return false;
}

// Converts a script number into a position in an array of `size` elements
// Returns false if the result isn't a valid position. Note -0.5 truncates to 0, so it IS valid
static bool toArrayIndex(double d, size_t size, size_t& out) {
    if (std::isnan(d) || d <= -1.0 || d >= static_cast<double>(size)) return false;
    out = static_cast<size_t>(std::trunc(d));
    return true;
}

bool VM::areVMEqual(const VMValue& a, const VMValue& b) {
    return a == b; // std::variant's operator== already returns false for differing alternatives rather than throwing, exactly the semantics the tree-walker's isEqual() wants
}

void VM::runtimeError(const std::string& message) {
    int line = -1;
    if (!frames.empty()) {
        const auto& frame = currentFrame();
        if (frame.ip > 0 && frame.ip - 1 < frame.closure->function->chunk.lines.size()) {
            line = frame.closure->function->chunk.lines[frame.ip - 1];
        }
    }
    std::cerr << "[line " << line << "] Runtime error: " << message << "\n";

    // Unwind the whole call stack so a subsequent REPL line (if any) starts clean rather than resuming mid-call 
    //matches the tree-walker's interpret() catching RuntimeError at the top level and abandoning the rest of that script/line's execution.
    frames.clear();
    stack.clear();
    openUpvalues.clear();
}

bool VM::callValue(const VMValue& callee, int argCount) {
    if (isVMClosure(callee)) {
        return call(asVMClosure(callee), argCount);
    }
    if (isVMNative(callee)) {
        auto native = asVMNative(callee); // copy the shared_ptr: `callee` refers into the stack, which is resized below
        if (argCount != native->arity) {
            std::ostringstream msg;
            msg << "Expected " << native->arity << " arguments but got " << argCount << ".";
            runtimeError(msg.str());
            return false;
        }
        std::vector<VMValue> args(stack.end() - argCount, stack.end());
        VMValue result;
        std::string error;
        if (!native->fn(args, result, error)) {
            runtimeError(error);
            return false;
        }
        // Natives have no CallFrame: drop the arguments and the callee, then leave the result where the call expression's value belongs.
        stack.resize(stack.size() - argCount - 1);
        push(std::move(result));
        return true;
    }
    if (isVMBoundMethod(callee)) {
        auto bound = asVMBoundMethod(callee); // copy first: `callee` refers to the very stack slot overwritten on the next line
        // A method's slot 0 holds its receiver instead of the callee (the compiler names that slot `this`), so put the receiver there and call the plain closure.
        stack[stack.size() - argCount - 1] = bound->receiver;
        return call(bound->method, argCount);
    }
    if (isVMClass(callee)) {
        auto klass = asVMClass(callee); // copy first, as above
        auto instance = std::make_shared<VMInstance>(klass);
        // The new instance takes the class's place on the stack. If there is an init(), it runs as a normal method call with the instance as `this`, and its compiled return hands the instance back as the call's value. With no init() there is nothing to run: the instance already sits where the call's value belongs.
        stack[stack.size() - argCount - 1] = VMValue{instance};
        auto initializer = klass->findMethod("init");
        if (initializer) {
            return call(initializer, argCount);
        }
        if (argCount != 0) {
            std::ostringstream msg;
            msg << "Expected 0 arguments but got " << argCount << ".";
            runtimeError(msg.str());
            return false;
        }
        return true;
    }
    runtimeError("Can only call functions.");
    return false;
}

bool VM::call(std::shared_ptr<VMClosure> closure, int argCount) {
    if (argCount != closure->function->arity) {
        std::ostringstream msg;
        msg << "Expected " << closure->function->arity << " arguments but got " << argCount << ".";
        runtimeError(msg.str());
        return false;
    }

    // stackBase points AT the callee value itself (slot 0), with arguments starting at slot 1. this matches the compiler reserving local slot 0 for the callee in EVERY function
    frames.push_back(CallFrame{closure, 0, stack.size() - argCount - 1, nullptr});
    return true;
}

std::shared_ptr<VMUpvalue> VM::captureUpvalue(size_t stackIndex) {
    // Reuse an existing OPEN upvalue for this exact slot if one exists
    for (auto& existing : openUpvalues) {
        if (!existing->isClosed && existing->stackIndex == stackIndex) {
            return existing;
        }
    }

    auto upvalue = std::make_shared<VMUpvalue>();
    upvalue->isClosed = false;
    upvalue->stackIndex = stackIndex;
    openUpvalues.push_back(upvalue);
    return upvalue;
}

void VM::closeUpvalues(size_t fromIndex) {
    for (auto it = openUpvalues.begin(); it != openUpvalues.end();) {
        if (!(*it)->isClosed && (*it)->stackIndex >= fromIndex) {
            (*it)->closedValue = stack[(*it)->stackIndex];
            (*it)->isClosed = true;
            it = openUpvalues.erase(it); // no longer needs tracking as "open"
        } else {
            ++it;
        }
    }
}

InterpretResult VM::run() {
    wideOperand = false;
    while (true) {
#ifdef MYLANG_GC_STRESS
        if (GC::shouldCollect()) GC::collect(); // test builds only: collect between ANY two instructions
#endif
        OpCode instruction = static_cast<OpCode>(readByte());

        switch (instruction) {
            case OpCode::OP_CONSTANT: {
                push(readConstant());
                break;
            }
            case OpCode::OP_WIDE: {
                // Only the compiler emits this, always directly before an instruction that reads a constant index. Check anyway so a stray prefix can't silently corrupt the next operand.
                auto next = static_cast<OpCode>(currentFrame().closure->function->chunk.code[currentFrame().ip]);
                switch (next) {
                    case OpCode::OP_CONSTANT: case OpCode::OP_DEFINE_GLOBAL: case OpCode::OP_GET_GLOBAL: case OpCode::OP_SET_GLOBAL:
                    case OpCode::OP_CLOSURE: case OpCode::OP_GET_PROPERTY: case OpCode::OP_SET_PROPERTY: case OpCode::OP_CLASS:
                    case OpCode::OP_METHOD: case OpCode::OP_GET_SUPER: case OpCode::OP_IMPORT:
                        wideOperand = true;
                        break;
                    default:
                        runtimeError("OP_WIDE before an instruction with no constant operand.");
                        return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_ADD: {
                // '+' overloads, matching the tree-walker: number+number adds, string+string concatenates, anything else is an error
                VMValue b = pop();
                VMValue a = pop();
                if (isVMNumber(a) && isVMNumber(b)) {
                    push(asVMNumber(a) + asVMNumber(b));
                } else if (isVMString(a) && isVMString(b)) {
                    push(asVMString(a) + asVMString(b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_SUBTRACT: {
                VMValue b = pop();
                VMValue a = pop();
                if (!requireNumbers(a, b, "-")) return InterpretResult::RUNTIME_ERROR;
                push(asVMNumber(a) - asVMNumber(b));
                break;
            }
            case OpCode::OP_MULTIPLY: {
                VMValue b = pop();
                VMValue a = pop();
                if (!requireNumbers(a, b, "*")) return InterpretResult::RUNTIME_ERROR;
                push(asVMNumber(a) * asVMNumber(b));
                break;
            }
            case OpCode::OP_DIVIDE: {
                VMValue b = pop();
                VMValue a = pop();
                if (!requireNumbers(a, b, "/")) return InterpretResult::RUNTIME_ERROR;
                if (asVMNumber(b) == 0.0) {
                    runtimeError("Division by zero.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                push(asVMNumber(a) / asVMNumber(b));
                break;
            }
            case OpCode::OP_MODULO: {
                VMValue b = pop();
                VMValue a = pop();
                if (!requireNumbers(a, b, "%")) return InterpretResult::RUNTIME_ERROR;
                if (asVMNumber(b) == 0.0) {
                    runtimeError("Modulo by zero.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                // fmod, not integer %, since numbers are doubles; matches the tree-walker
                push(std::fmod(asVMNumber(a), asVMNumber(b)));
                break;
            }
            case OpCode::OP_NEGATE: {
                VMValue a = pop();
                if (!isVMNumber(a)) {
                    runtimeError("Operand to unary '-' must be a number.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                push(-asVMNumber(a));
                break;
            }
            case OpCode::OP_PRINT: {
                VMValue value = pop();
                std::cout << stringifyVMValue(value) << "\n";
                break;
            }
            case OpCode::OP_POP: {
                pop();
                break;
            }
            case OpCode::OP_DEFINE_GLOBAL: {
                std::string name = asVMString(readConstant());
                currentGlobals()[name] = pop();
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                std::string name = asVMString(readConstant());
                auto& globals = currentGlobals();
                auto it = globals.find(name);
                if (it == globals.end()) {
                    runtimeError("Undefined variable '" + name + "'.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                push(it->second);
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                std::string name = asVMString(readConstant());
                auto& globals = currentGlobals();
                if (globals.find(name) == globals.end()) {
                    runtimeError("Undefined variable '" + name + "'.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                globals[name] = peekStack(0);
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = readByte();
                push(stack[currentFrame().stackBase + slot]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = readByte();
                stack[currentFrame().stackBase + slot] = peekStack(0);
                break;
            }
            case OpCode::OP_TRUE: {
                push(true);
                break;
            }
            case OpCode::OP_FALSE: {
                push(false);
                break;
            }
            case OpCode::OP_NIL: {
                push(std::monostate{});
                break;
            }
            case OpCode::OP_NOT: {
                push(!isVMTruthy(pop()));
                break;
            }
            case OpCode::OP_EQUAL: {
                VMValue b = pop();
                VMValue a = pop();
                push(areVMEqual(a, b));
                break;
            }
            case OpCode::OP_GREATER: {
                VMValue b = pop();
                VMValue a = pop();
                if (!requireNumbers(a, b, ">")) return InterpretResult::RUNTIME_ERROR;
                push(asVMNumber(a) > asVMNumber(b));
                break;
            }
            case OpCode::OP_LESS: {
                VMValue b = pop();
                VMValue a = pop();
                if (!requireNumbers(a, b, "<")) return InterpretResult::RUNTIME_ERROR;
                push(asVMNumber(a) < asVMNumber(b));
                break;
            }
            case OpCode::OP_JUMP: {
                uint16_t offset = readShort();
                currentFrame().ip += offset;
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = readShort();
                if (!isVMTruthy(peekStack(0))) {
                    currentFrame().ip += offset;
                }
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = readShort();
                currentFrame().ip -= offset;
                // A loop is where a script can allocate without end, so a loop is where the collector gets its chance (calls are the other place, below).
                if (GC::shouldCollect()) GC::collect();
                break;
            }
            case OpCode::OP_CALL: {
                int argCount = readByte();
                if (GC::shouldCollect()) GC::collect(); // the callee and its arguments are on the stack, so they count as roots
                if (!callValue(peekStack(argCount), argCount)) {
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_CLOSURE: {
                auto function = asVMFunction(readConstant());
                auto closure = std::make_shared<VMClosure>();
                closure->function = function;
                closure->module = currentFrame().closure->module; // a closure belongs to the module whose code created it

                // Read exactly `function->upvalueCount` (isLocal, index) pairs
                for (int i = 0; i < function->upvalueCount; i++) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();
                    if (isLocal) {
                        // Captures a LIVE local slot from the CURRENT (enclosing) call
                        closure->upvalues.push_back(captureUpvalue(currentFrame().stackBase + index));
                    } else {
                        // Not a fresh capture: this closure just reuses an upvalue the ENCLOSING closure already has
                        closure->upvalues.push_back(currentFrame().closure->upvalues[index]);
                    }
                }

                push(VMValue{closure});
                break;
            }
            case OpCode::OP_GET_UPVALUE: {
                uint8_t slot = readByte();
                auto& upvalue = currentFrame().closure->upvalues[slot];
                push(upvalue->isClosed ? upvalue->closedValue : stack[upvalue->stackIndex]);
                break;
            }
            case OpCode::OP_SET_UPVALUE: {
                uint8_t slot = readByte();
                auto& upvalue = currentFrame().closure->upvalues[slot];
                if (upvalue->isClosed) {
                    upvalue->closedValue = peekStack(0);
                } else {
                    stack[upvalue->stackIndex] = peekStack(0);
                }
                break;
            }
            case OpCode::OP_CLOSE_UPVALUE: {
                // Close (if any open upvalue refers to it) the CURRENT op-of-stack slot, preserving its value independent of the stack, then discard the slot itsel
                closeUpvalues(stack.size() - 1);
                pop();
                break;
            }
            case OpCode::OP_ARRAY: {
                size_t count = readShort();
                auto array = std::make_shared<VMArray>();
                // The element values sit on top of the stack in source order; move them into the array and drop their slots.
                auto first = stack.end() - static_cast<std::ptrdiff_t>(count);
                array->elements.assign(std::make_move_iterator(first), std::make_move_iterator(stack.end()));
                stack.erase(first, stack.end());
                push(VMValue{array});
                break;
            }
            case OpCode::OP_GET_INDEX: {
                VMValue index = pop();
                VMValue object = pop();
                if (isVMArray(object)) {
                    if (!isVMNumber(index)) {
                        runtimeError("Array index must be a number.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    auto array = asVMArray(object);
                    size_t i = 0;
                    if (!toArrayIndex(asVMNumber(index), array->elements.size(), i)) {
                        runtimeError("Array index out of range.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(array->elements[i]);
                    break;
                }
                if (isVMMap(object)) {
                    if (!isVMString(index)) {
                        runtimeError("Map key must be a string.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    auto map = asVMMap(object);
                    auto found = map->entries.find(asVMString(index));
                    if (found == map->entries.end()) {
                        runtimeError("Undefined map key '" + asVMString(index) + "'.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(found->second);
                    break;
                }
                runtimeError("Only arrays and maps can be indexed.");
                return InterpretResult::RUNTIME_ERROR;
            }
            case OpCode::OP_SET_INDEX: {
                VMValue value = pop();
                VMValue index = pop();
                VMValue object = pop();
                if (isVMArray(object)) {
                    if (!isVMNumber(index)) {
                        runtimeError("Array index must be a number.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    auto array = asVMArray(object);
                    size_t i = 0;
                    if (!toArrayIndex(asVMNumber(index), array->elements.size(), i)) {
                        runtimeError("Array index out of range.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    array->elements[i] = value;
                    push(value); // assignment is an expression: its value is the assigned value
                    break;
                }
                if (isVMMap(object)) {
                    if (!isVMString(index)) {
                        runtimeError("Map key must be a string.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    // Unlike arrays (fixed positions), maps grow freely: assigning a new key inserts it.
                    asVMMap(object)->entries[asVMString(index)] = value;
                    push(value);
                    break;
                }
                runtimeError("Only arrays and maps can be indexed.");
                return InterpretResult::RUNTIME_ERROR;
            }
            case OpCode::OP_MAP: {
                size_t count = readShort();
                auto map = std::make_shared<VMMap>();
                // The stack holds key, value, key, value, ... in source order. Assigning in that order means a repeated key keeps its LAST value, like the tree-walker.
                auto first = stack.end() - static_cast<std::ptrdiff_t>(count * 2);
                for (auto it = first; it != stack.end(); it += 2) {
                    map->entries[asVMString(*it)] = std::move(*(it + 1));
                }
                stack.erase(first, stack.end());
                push(VMValue{map});
                break;
            }
            case OpCode::OP_DUP2: {
                // Copy both first: push() may reallocate the stack, which would invalidate references into it.
                VMValue below = peekStack(1);
                VMValue top = peekStack(0);
                push(below);
                push(top);
                break;
            }
            case OpCode::OP_GET_PROPERTY: {
                std::string name = asVMString(readConstant()); // copy: readConstant() returns a temporary
                VMValue object = pop();
                if (isVMInstance(object)) {
                    auto instance = asVMInstance(object);
                    // A field shadows a method of the same name, like the tree-walker's LoxInstance::get.
                    auto field = instance->fields.find(name);
                    if (field != instance->fields.end()) {
                        push(field->second);
                        break;
                    }
                    auto method = instance->klass->findMethod(name);
                    if (!method) {
                        runtimeError("Undefined property '" + name + "'.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(VMValue{std::make_shared<VMBoundMethod>(object, method)});
                    break;
                }
                if (isVMArray(object)) {
                    VMValue method;
                    if (!getVMArrayMethod(asVMArray(object), name, method)) {
                        runtimeError("Undefined array method '" + name + "'.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(std::move(method));
                    break;
                }
                if (isVMMap(object)) {
                    VMValue method;
                    if (!getVMMapMethod(asVMMap(object), name, method)) {
                        runtimeError("Undefined map method '" + name + "'.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(std::move(method));
                    break;
                }
                if (isVMModule(object)) {
                    // m.name reads a top-level binding of the imported file (live: it sees later changes the module makes to its own variables). Unlike an instance, there are no methods to bind.
                    auto module = asVMModule(object);
                    auto found = module->globals.find(name);
                    if (found == module->globals.end()) {
                        runtimeError("Undefined variable '" + name + "'.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(found->second);
                    break;
                }
                runtimeError("Only instances, arrays, maps, and modules have properties.");
                return InterpretResult::RUNTIME_ERROR;
            }
            case OpCode::OP_IMPORT: {
                std::string path = asVMString(readConstant()); // copy: readConstant() returns a temporary
                if (!hasModuleBaseDir) {
                    runtimeError("Imports are not available in this context (no base directory set).");
                    return InterpretResult::RUNTIME_ERROR;
                }
                std::string resolved = resolveModulePath(path);

                auto cached = moduleCache.find(resolved);
                if (cached != moduleCache.end()) {
                    push(VMValue{cached->second}); // already imported somewhere: reuse the same module object, don't run the file again
                    break;
                }
                if (currentlyLoading.count(resolved)) {
                    runtimeError("Circular import detected for '" + path + "'.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                std::ifstream file(resolved);
                if (!file) {
                    runtimeError("Could not open imported file '" + path + "'.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                std::stringstream buffer;
                buffer << file.rdbuf();

                Compiler importCompiler; // syntax errors in the file are printed by the compiler, then the import fails
                std::shared_ptr<VMFunction> script = importCompiler.compile(buffer.str());
                if (!script) {
                    runtimeError("Could not compile imported file '" + path + "'.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                auto module = std::make_shared<VMModule>();
                module->path = resolved;
                registerVMStdlib(module->globals); // each module has its own copy of the standard library, like the tree-walker's per-module Interpreter

                auto closure = std::make_shared<VMClosure>();
                closure->function = script;
                closure->module = module.get();

                // Run the file's top-level code as an ordinary call with no arguments. No recursion into run(): this is just one more frame, so a long chain of imports doesn't use C++ stack.
                // OP_RETURN from this frame caches the module and pushes it as the import's value.
                currentlyLoading.insert(resolved);
                push(VMValue{closure});
                frames.push_back(CallFrame{closure, 0, stack.size() - 1, module});
                break;
            }
            case OpCode::OP_CLASS: {
                std::string name = asVMString(readConstant());
                push(VMValue{std::make_shared<VMClass>(name)});
                break;
            }
            case OpCode::OP_INHERIT: {
                // [.. superclass subclass]
                if (!isVMClass(peekStack(1))) {
                    runtimeError("Superclass must be a class.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                asVMClass(peekStack(0))->superclass = asVMClass(peekStack(1));
                pop(); // the subclass; the superclass stays on the stack as the hidden local `super`
                break;
            }
            case OpCode::OP_METHOD: {
                std::string name = asVMString(readConstant());
                // [.. class closure]
                asVMClass(peekStack(1))->methods[name] = asVMClosure(peekStack(0));
                pop();
                break;
            }
            case OpCode::OP_GET_SUPER: {
                std::string name = asVMString(readConstant());
                VMValue superclass = pop();
                VMValue instance = pop();
                // `super` is only ever a class here: OP_INHERIT already rejected anything else when the class was declared.
                auto method = asVMClass(superclass)->findMethod(name);
                if (!method) {
                    runtimeError("Undefined property '" + name + "'.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                push(VMValue{std::make_shared<VMBoundMethod>(instance, method)});
                break;
            }
            case OpCode::OP_CHECK_FIELDS: {
                if (!isVMInstance(peekStack(0))) {
                    runtimeError("Only instances have fields.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_SET_PROPERTY: {
                std::string name = asVMString(readConstant());
                VMValue value = pop();
                VMValue object = pop();
                if (!isVMInstance(object)) { // the compiler already emitted OP_CHECK_FIELDS, but this keeps the opcode safe on its own
                    runtimeError("Only instances have fields.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                asVMInstance(object)->fields[name] = value; // creates the field on first assignment
                push(value);
                break;
            }
            case OpCode::OP_DUP: {
                VMValue top = peekStack(0); // copy first: push() may reallocate the stack
                push(top);
                break;
            }
            case OpCode::OP_RETURN: {
                VMValue result = pop();
                size_t returningFromStackBase = currentFrame().stackBase;
                std::shared_ptr<VMModule> finishedImport = currentFrame().importedModule; // copy: the frame is popped below

                // Close any of THIS call's locals that some closure captured (must happen BEFORE the stack is truncated below, or open upvalues would dangle)
                closeUpvalues(returningFromStackBase);

                frames.pop_back();

                if (frames.empty()) {
                    // The top-level script itself returned i.e the whole program is done.
                    return InterpretResult::OK;
                }

                // Discard everything the just-finished call left behind its callee value (at stackBase), all its arguments and locals (stackBase+1 and up) 
                // then push the return value where the caller can use it as this whole call-expression's result.
                stack.resize(returningFromStackBase);
                if (finishedImport) {
                    // An imported file finished its top-level code: the import expression's value is the module, and from now on it is cached.
                    currentlyLoading.erase(finishedImport->path);
                    moduleCache[finishedImport->path] = finishedImport;
                    push(VMValue{finishedImport});
                } else {
                    push(result);
                }
                break;
            }
            default: {
                // Only reachable through a VM/compiler bug (e.g. a new opcode with no case above), never from a user script.
                runtimeError("Unknown opcode " + std::to_string(static_cast<int>(instruction)) + ".");
                return InterpretResult::RUNTIME_ERROR;
            }
        }
    }
}
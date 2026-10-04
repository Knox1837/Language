// vm.cpp: the actual bytecode execution loop. 
#include "vm.h"
#include "compiler.h"
#include "vm_native.h"
#include "vm_stdlib.h"
#include <cmath>
#include <iostream>
#include <sstream>

VM::VM() {
    registerVMStdlib(globals);
}

InterpretResult VM::interpret(const std::string& source) {
    stack.clear();
    frames.clear();
    openUpvalues.clear();

    Compiler compiler;
    std::shared_ptr<VMFunction> script = compiler.compile(source);
    if (!script) {
        return InterpretResult::COMPILE_ERROR;
    }

    // The top-level script is wrapped in a VMClosure too (with zero upvalues , nothing encloses it), built directly here rather than via OP_CLOSURE bytecode, since there's no OUTER chunk for such an instruction to live in. 
    // This keeps it on the same CallFrame footing as any real function from this point on.
    auto scriptClosure = std::make_shared<VMClosure>();
    scriptClosure->function = script;

    push(VMValue{scriptClosure}); // occupies slot 0, same convention every real call uses
    frames.push_back(CallFrame{scriptClosure, 0, 0});

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
    return currentFrame().closure->function->chunk.constants[readByte()];
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
    frames.push_back(CallFrame{closure, 0, stack.size() - argCount - 1});
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
    while (true) {
        OpCode instruction = static_cast<OpCode>(readByte());

        switch (instruction) {
            case OpCode::OP_CONSTANT: {
                push(readConstant());
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
                globals[name] = pop();
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                std::string name = asVMString(readConstant());
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
                break;
            }
            case OpCode::OP_CALL: {
                int argCount = readByte();
                if (!callValue(peekStack(argCount), argCount)) {
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_CLOSURE: {
                auto function = asVMFunction(readConstant());
                auto closure = std::make_shared<VMClosure>();
                closure->function = function;

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
            case OpCode::OP_RETURN: {
                VMValue result = pop();
                size_t returningFromStackBase = currentFrame().stackBase;

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
                push(result);
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
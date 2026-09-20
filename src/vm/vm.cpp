// vm.cpp: the actual bytecode execution loop. 
#include "vm.h"
#include "compiler.h"
#include <iostream>
#include <sstream>

InterpretResult VM::interpret(const std::string& source) {
    stack.clear();
    frames.clear();

    Compiler compiler;
    std::shared_ptr<VMFunction> script = compiler.compile(source);
    if (!script) {
        return InterpretResult::COMPILE_ERROR;
    }

    push(VMValue{script}); // the script "function" itself occupies slot 0, mirroring how
                             // any callable sits on the stack just below its call frame
    frames.push_back(CallFrame{script, 0, 0});

    return run();
}

uint8_t VM::readByte() {
    return currentFrame().function->chunk.code[currentFrame().ip++];
}

uint16_t VM::readShort() {
    uint8_t high = readByte();
    uint8_t low = readByte();
    return (static_cast<uint16_t>(high) << 8) | low;
}

VMValue VM::readConstant() {
    return currentFrame().function->chunk.constants[readByte()];
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
        if (frame.ip > 0 && frame.ip - 1 < frame.function->chunk.lines.size()) {
            line = frame.function->chunk.lines[frame.ip - 1];
        }
    }
    std::cerr << "[line " << line << "] Runtime error: " << message << "\n";

    // Unwind the whole call stack so a subsequent REPL line (if any) starts clean rather than resuming mid-call 
    //matches the tree-walker's interpret() catching RuntimeError at the top level and abandoning the rest of that script/line's execution.
    frames.clear();
    stack.clear();
}

bool VM::callValue(const VMValue& callee, int argCount) {
    if (isVMFunction(callee)) {
        return call(asVMFunction(callee), argCount);
    }
    runtimeError("Can only call functions.");
    return false;
}

bool VM::call(std::shared_ptr<VMFunction> function, int argCount) {
    if (argCount != function->arity) {
        std::ostringstream msg;
        msg << "Expected " << function->arity << " arguments but got " << argCount << ".";
        runtimeError(msg.str());
        return false;
    }

    // stackBase points AT the callee value itself (slot 0), with
    // arguments starting at slot 1 — this matches the compiler
    // reserving local slot 0 for the callee in EVERY function (see
    // Compiler::functionBody()'s comment), including the top-level
    // script (where slot 0 is the script's own function value, pushed
    // in VM::interpret() before its frame is created).
    frames.push_back(CallFrame{function, 0, stack.size() - argCount - 1});
    return true;
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
                // '+' is currently number-only in the VM (no string concatenation yet) 
                VMValue b = pop();
                VMValue a = pop();
                if (!requireNumbers(a, b, "+")) return InterpretResult::RUNTIME_ERROR;
                push(asVMNumber(a) + asVMNumber(b));
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
                if (isVMNumber(value)) {
                    std::cout << asVMNumber(value) << "\n";
                } else if (isVMString(value)) {
                    std::cout << asVMString(value) << "\n";
                } else if (isVMBool(value)) {
                    std::cout << (asVMBool(value) ? "true" : "false") << "\n";
                } else if (isVMFunction(value)) {
                    std::cout << "<fn " << asVMFunction(value)->name << ">\n";
                } else {
                    std::cout << "nil\n";
                }
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
                // Assignment is itself an expression (matches the tree-walker's Assign node)
                globals[name] = peekStack(0);
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                // A local's "address" is a stack index, resolved entirely at compile time.
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
                // PEEKS, does not pop 
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
                // The callee sits argCount slots below the top of the stack (its arguments are all above it).
                if (!callValue(peekStack(argCount), argCount)) {
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_RETURN: {
                VMValue result = pop();
                size_t returningFromStackBase = currentFrame().stackBase;
                frames.pop_back();

                if (frames.empty()) {
                    // The top-level script itself returned i.e the whole program is done.
                    return InterpretResult::OK;
                }

                // Discard everything the just-finished call left behind
                // its callee value (at stackBase), all its arguments and locals (stackBase+1 and up) 
                // then push the return value where the caller can use it as this whole call-expression's result.
                stack.resize(returningFromStackBase);
                push(result);
                break;
            }
        }
    }
}
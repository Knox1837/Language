// vm.cpp: the actual bytecode execution loop. 
#include "vm.h"
#include "compiler.h"
#include <iostream>
#include <sstream>

InterpretResult VM::interpret(const std::string& source) {
    chunk = Chunk(); // fresh chunk per call — fine for now; a REPL that wants to preserve state across lines is a later concern
    ip = 0;

    Compiler compiler;
    if (!compiler.compile(source, chunk)) {
        return InterpretResult::COMPILE_ERROR;
    }

    return run();
}

uint8_t VM::readByte() {
    return chunk.code[ip++];
}

VMValue VM::readConstant() {
    return chunk.constants[readByte()];
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

void VM::runtimeError(const std::string& message) {
    int line = (ip > 0 && ip - 1 < chunk.lines.size()) ? chunk.lines[ip - 1] : -1;
    std::cerr << "[line " << line << "] Runtime error: " << message << "\n";
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
                push(stack[slot]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = readByte();
                // Same "peek, don't pop" reasoning as OP_SET_GLOBAL- assignment is an expression, so its value stays on top of the stack for whatever comes next.
                stack[slot] = peekStack(0);
                break;
            }
            case OpCode::OP_RETURN: {
                return InterpretResult::OK;
            }
        }
    }
}
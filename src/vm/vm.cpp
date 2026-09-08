// vm.cpp: the actual bytecode execution loop. 
#include "vm.h"
#include "compiler.h"
#include <iostream>

InterpretResult VM::interpret(const std::string& source) {
    chunk = Chunk(); // fresh chunk per call
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
    stack.push_back(value);
}

VMValue VM::pop() {
    VMValue value = stack.back();
    stack.pop_back();
    return value;
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
                VMValue constant = readConstant();
                push(constant);
                break;
            }
            case OpCode::OP_ADD: {
                VMValue b = pop();
                VMValue a = pop();
                push(a + b);
                break;
            }
            case OpCode::OP_SUBTRACT: {
                VMValue b = pop();
                VMValue a = pop();
                push(a - b);
                break;
            }
            case OpCode::OP_MULTIPLY: {
                VMValue b = pop();
                VMValue a = pop();
                push(a * b);
                break;
            }
            case OpCode::OP_DIVIDE: {
                VMValue b = pop();
                VMValue a = pop();
                if (b == 0.0) {
                    runtimeError("Division by zero.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                push(a / b);
                break;
            }
            case OpCode::OP_NEGATE: {
                push(-pop());
                break;
            }
            case OpCode::OP_PRINT: {
                VMValue value = pop();
                std::cout << value << "\n";
                break;
            }
            case OpCode::OP_RETURN: {
                return InterpretResult::OK;
            }
        }
    }
}
// opcode.h: the instruction set. 
// Each opcode is one byte with some followed by operands

#pragma once
#include <cstdint>

enum class OpCode : uint8_t {
    OP_CONSTANT,   // push chunk.constants[operand] onto the value stack
    OP_ADD,        // pop b, pop a, push (a + b)
    OP_SUBTRACT,   // pop b, pop a, push (a - b)
    OP_MULTIPLY,   // pop b, pop a, push (a * b)
    OP_DIVIDE,     // pop b, pop a, push (a / b)
    OP_NEGATE,     // pop a, push (-a)
    OP_PRINT,      // pop a, print it
    OP_RETURN,     // stop execution (temporary — real semantics come with functions)
};
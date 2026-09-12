// opcode.h: the instruction set. 
// Each opcode is one byte with some followed by operands
#pragma once
#include <cstdint>

enum class OpCode : uint8_t {
    OP_CONSTANT,       // push chunk.constants[operand] onto the value stack
    OP_ADD,            // pop b, pop a, push (a + b)
    OP_SUBTRACT,       // pop b, pop a, push (a - b)
    OP_MULTIPLY,       // pop b, pop a, push (a * b)
    OP_DIVIDE,         // pop b, pop a, push (a / b)
    OP_NEGATE,         // pop a, push (-a)
    OP_PRINT,          // pop a, print it
    OP_POP,            // pop and discard (used to clean up an expression-statement's unused result)
    OP_DEFINE_GLOBAL,  // pop value, bind it to the name at chunk.constants[operand] in the globals table
    OP_GET_GLOBAL,     // look up the name at chunk.constants[operand] in globals, push its value
    OP_SET_GLOBAL,     // peek (don't pop) the top of stack, store it into the EXISTING global named
                        // at chunk.constants[operand] — errors if that global was never defined
    OP_GET_LOCAL,      // push a COPY of stack[operand] (a local variable's slot on the value stack itself)
    OP_SET_LOCAL,      // peek (don't pop) the top of stack, store it into stack[operand]
    OP_TRUE,           // push the boolean true
    OP_FALSE,          // push the boolean false
    OP_NIL,            // push nil
    OP_NOT,            // pop a, push (!isVMTruthy(a)) -- always yields a real bool, unlike '!' in some languages
    OP_EQUAL,          // pop b, pop a, push (a == b) -- works across any two value types, like the tree-walker's isEqual
    OP_GREATER,        // pop b, pop a, push (a > b) -- numbers only
    OP_LESS,           // pop b, pop a, push (a < b) -- numbers only
    OP_JUMP,           // unconditional jump: ip += operand (a 2-byte offset, see chunk.h)
    OP_JUMP_IF_FALSE,  // PEEKS the top of stack (does not pop); if !isVMTruthy(peek), ip += operand.
                        // Peeking (not popping) is deliberate: if/while explicitly OP_POP the
                        // condition themselves afterward, and and_/or_ rely on the value
                        // surviving on the stack as their short-circuit result.
    OP_LOOP,           // unconditional BACKWARD jump: ip -= operand (used to jump back to a loop's condition)
    OP_RETURN,         // stop execution (temporary — real semantics come with functions)
};
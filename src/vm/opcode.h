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
    OP_MODULO,         // pop b, pop a, push fmod(a, b) -- sign follows the dividend, like the tree-walker; b == 0 is an error
    OP_NEGATE,         // pop a, push (-a)
    OP_PRINT,          // pop a, print it
    OP_POP,            // pop and discard (used to clean up an expression-statement's unused result)
    OP_DEFINE_GLOBAL,  // pop value, bind it to the name at chunk.constants[operand] in the globals table
    OP_GET_GLOBAL,     // look up the name at chunk.constants[operand] in globals, push its value
    OP_SET_GLOBAL,     // peek (don't pop) the top of stack, store it into the EXISTING global named at chunk.constants[operand] — errors if that global was never defined
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
    OP_JUMP_IF_FALSE,  // PEEKS the top of stack (does not pop); if !isVMTruthy(peek), ip += operand. Peeking (not popping) is deliberate: if/while explicitly OP_POP the condition themselves afterward, and and_/or_ rely on the value surviving on the stack as their short-circuit result.
    OP_LOOP,           // unconditional BACKWARD jump: ip -= operand (used to jump back to a loop's condition)
    OP_CALL,           // call the callable value sitting `operand` slots below the top of the stack (i.e. below its `operand` arguments, which sit above it) - pushes a new CallFrame and transfers execution into the function's own bytecode
    OP_CLOSURE,        // reads a VMFunction constant (operand: its constant-pool index), then reads `function->upvalueCount` more (isLocal, index) byte-pairs describing how to capture each upvalue, builds a VMClosure, and pushes it
    OP_GET_UPVALUE,    // push a COPY of the value the current closure's upvalues[operand] refers to
    OP_SET_UPVALUE,    // peek (don't pop) the top of stack, store it into the current closure's upvalues[operand]
    OP_CLOSE_UPVALUE,  // "closes" the upvalue (if any) referring to the CURRENT top-of-stack slot - copies its value out of the stack into heap-owned storage so it survives after this slot is popped/reused — then pops that slot. Emitted instead of a plain OP_POP specifically for a local that some closure captured.
    OP_RETURN,         // pop the return value, close any of the current call's still-open upvalues, pop the current CallFrame, resume the caller with that return value pushed onto ITS stack. At the top level (no caller), this means the program is done.
    OP_ARRAY,          // operand: a 2-byte element count N. Pops the top N values (in the order they were pushed), builds an array from them, pushes it
    OP_GET_INDEX,      // pop index, pop object, push object[index]: an array (numeric index, must be in range) or a map (string key, must exist)
    OP_SET_INDEX,      // pop value, pop index, pop object, store object[index] = value, then push the VALUE back (assignment is an expression). On a map this also INSERTS a new key
    OP_DUP2,           // duplicate the top TWO values: [.. a b] becomes [.. a b a b]. Used by compound assignment to a[i] so that `a` and `i` are evaluated ONCE
    OP_GET_PROPERTY,   // operand: constant-pool index of a name. Pops an object, pushes its property of that name: an instance's field (else its method, bound to it), or an array's/map's method bound to it
    OP_CLASS,          // operand: constant-pool index of the class name. Pushes a new, empty class with that name
    OP_INHERIT,        // stack [.. superclass subclass]: errors unless the superclass really is a class, links subclass -> superclass, then pops the SUBCLASS (the superclass stays, as the hidden local `super`)
    OP_METHOD,         // operand: constant-pool index of the method name. Stack [.. class closure]: stores the closure as that method of the class and pops it
    OP_GET_SUPER,      // operand: constant-pool index of a method name. Stack [.. this superclass]: pops both, pushes the superclass's method of that name bound to `this`
    OP_SET_PROPERTY,   // operand: constant-pool index of a field name. Stack [.. instance value]: sets (or creates) the field, pops both, pushes the VALUE back (assignment is an expression)
    OP_CHECK_FIELDS,   // PEEKS the top of the stack and errors unless it is an instance. Emitted before the right side of `obj.x = v` / `obj.x += v`, so a bad target fails BEFORE the right side runs, like the tree-walker
    OP_DUP,            // duplicate the top value: [.. a] becomes [.. a a]. Used by `obj.x += v` so `obj` is evaluated ONCE
    OP_MAP,            // operand: a 2-byte entry count N. Pops 2N values laid out key, value, key, value, ... (keys are strings), builds a map from them (a repeated key keeps the LAST value), pushes it
    OP_WIDE,           // prefix: the NEXT instruction's constant-pool operand is 2 bytes (big-endian) instead of 1. Emitted only before an instruction that takes a constant index, and only when the index is above 255
    OP_IMPORT,         // operand: constant-pool index of the path string. Loads the file (or reuses the cached module) and leaves the module object on the stack. A first-time load pushes a call frame for the file's top-level code, and OP_RETURN from that frame supplies the module object instead of nil
    OP_ITER_PREP,      // pops-and-replaces the top value, the thing a for-in loop walks: an array stays as it is, a map becomes a new array of its keys (a snapshot, in the map's sorted key order); anything else is an error
    OP_ITER_NEXT,      // operand: a 2-byte forward jump offset. Stack [.. array index]. If index is past the end of the array, jumps ahead by the offset (the loop is over, nothing pushed); otherwise pushes array[index] and adds 1 to the index IN PLACE. The array's length is re-read each time, so it may change under the loop
};
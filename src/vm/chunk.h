// chunk.h: a Chunk is one compiled unit of bytecode: a flat array of instruction bytes, a "constant pool" of values referenced by
// OP_CONSTANT, and a parallel array of source line numbers (one per byte in `code`) so runtime errors can report a real line number.

#pragma once
#include <vector>
#include <cstdint>
#include "opcode.h"
#include "vm_value.h"

class Chunk {
public:
    std::vector<uint8_t> code;
    std::vector<VMValue> constants;
    std::vector<int> lines; // lines[i] is the source line that produced code[i]

    // Appends one raw byte (an opcode or an operand) tagged with the
    // source line it came from.
    void write(uint8_t byte, int line);
    void write(OpCode op, int line);

    // Adds a value to the constant pool and returns its index (used as OP_CONSTANT's operand byte).
    int addConstant(VMValue value);

    // Overwrites the 2-byte jump offset already written at `code[offset]` and `code[offset+1]` — used for backpatching: a jump's destination often isn't known until AFTER its body has been compiled
    void patchJumpAt(size_t offset, uint16_t jumpDistance);
};
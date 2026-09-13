// chunk.cpp, trivial: just appends to the parallel vectors.

#include "chunk.h"
#include <stdexcept>

void Chunk::write(uint8_t byte, int line) {
    code.push_back(byte);
    lines.push_back(line);
}

void Chunk::write(OpCode op, int line) {
    write(static_cast<uint8_t>(op), line);
}

int Chunk::addConstant(VMValue value) {
    if (constants.size() >= 256) {
        throw std::runtime_error("Too many constants in one chunk (limit 256 for this increment).");
    }
    constants.push_back(value);
    return static_cast<int>(constants.size() - 1);
}

void Chunk::patchJumpAt(size_t offset, uint16_t jumpDistance) {
    // Big-endian 2-byte write: matches how the VM reads it back (see VM::run()'s OP_JUMP/OP_JUMP_IF_FALSE/OP_LOOP cases).
    code[offset] = static_cast<uint8_t>((jumpDistance >> 8) & 0xFF);
    code[offset + 1] = static_cast<uint8_t>(jumpDistance & 0xFF);
}
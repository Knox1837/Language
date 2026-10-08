// chunk.cpp, trivial: just appends to the parallel vectors.

#include "chunk.h"
#include <cmath>

void Chunk::write(uint8_t byte, int line) {
    code.push_back(byte);
    lines.push_back(line);
}

void Chunk::write(OpCode op, int line) {
    write(static_cast<uint8_t>(op), line);
}

// Two constants are interchangeable only if they are the same kind of value AND the same value.
// Only numbers and strings are ever shared: they are immutable, and they are exactly what a script repeats (a variable name used twenty times, the literal 1).
// Function constants are never shared -- each compiled function is its own object.
static bool sameConstant(const VMValue& a, const VMValue& b) {
    if (isVMNumber(a) && isVMNumber(b)) {
        double x = asVMNumber(a), y = asVMNumber(b);
        return x == y && std::signbit(x) == std::signbit(y); // the signbit check keeps 0.0 and -0.0 apart, since == treats them as equal
    }
    if (isVMString(a) && isVMString(b)) return asVMString(a) == asVMString(b);
    return false;
}

int Chunk::addConstant(VMValue value) {
    // Reuse an existing slot when possible. This runs BEFORE the limit check, so a full pool can still be referenced again.
    // A linear scan as the pool never holds more than 256 entries.
    for (size_t i = 0; i < constants.size(); i++) {
        if (sameConstant(constants[i], value)) return static_cast<int>(i);
    }
    if (constants.size() >= 256) return -1; // a constant's index is a single operand byte; the compiler reports this as a normal compile error
    constants.push_back(std::move(value));
    return static_cast<int>(constants.size() - 1);
}

void Chunk::patchJumpAt(size_t offset, uint16_t jumpDistance) {
    // Big-endian 2-byte write: matches how the VM reads it back (see VM::run()'s OP_JUMP/OP_JUMP_IF_FALSE/OP_LOOP cases).
    code[offset] = static_cast<uint8_t>((jumpDistance >> 8) & 0xFF);
    code[offset + 1] = static_cast<uint8_t>(jumpDistance & 0xFF);
}
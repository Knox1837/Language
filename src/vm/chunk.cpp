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
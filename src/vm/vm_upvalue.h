// vm_upvalue.h: an upvalue is how a closure keeps referencing a variable that lives in an ENCLOSING function's stack frame
// even after that enclosing call has returned (at which point its stack slots have been reused for something else entirely).

#pragma once
#include "vm_value.h"
#include <cstddef>

class VMUpvalue {
public:
    bool isClosed = false;
    // If open: the index of the stack slot this upvalue is capturing.
    size_t stackIndex = 0;

    // While closed: the value itself, copied out of the stack once the enclosing call returned and that stack slot was about to be reused.
    VMValue closedValue;
};
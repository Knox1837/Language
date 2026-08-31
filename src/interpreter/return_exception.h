#pragma once
// return_exception.h — a "return" statement needs to unwind out of however many nested blocks/ifs/loops it's inside, immediately, with a  value.
// This is not an error condition, so we don't want to throw a runtime_error or similar. Instead we throw this special exception type, which the interpreter catches and uses to return from the current function.

#include "value.h"

struct ReturnException {
    Value value;
    explicit ReturnException(Value value) : value(std::move(value)) {}
};
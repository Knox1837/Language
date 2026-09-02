// native_function.cpp: trivial: just stores the wrapped function and  forwards call()/arity() to it.

#include "native_function.h"

NativeFunction::NativeFunction(std::string name, int arity, Fn fn)
    : name(std::move(name)), arityCount(arity), fn(std::move(fn)) {}

int NativeFunction::arity() const {
    return arityCount;
}

Value NativeFunction::call(Interpreter& interpreter, std::vector<Value>& arguments) {
    return fn(interpreter, arguments);
}

std::string NativeFunction::toString() const {
    return "<native fn " + name + ">";
}
// callable.h: the abstract interface every "thing you can call with ()" implements. Allows for addition of new native callable functions 
#pragma once
#include <vector>
#include <string>
#include "value.h"

class Interpreter; // forward declared to avoid a circular include with interpreter.h
// "Expected 2 arguments but got 1.", or for a function that takes a range, "Expected 1 or 2 arguments but got 3."
inline std::string arityMessage(int minArity, int maxArity, std::size_t got) {
    std::string expected = std::to_string(minArity);
    if (maxArity == minArity + 1) expected += " or " + std::to_string(maxArity);
    else if (maxArity > minArity + 1) expected += " to " + std::to_string(maxArity);
    return "Expected " + expected + " arguments but got " + std::to_string(got) + ".";
}

class Callable {
public:
    virtual int arity() const = 0;                                          // expected argument count (the MINIMUM, for a function with optional arguments)
    virtual int maxArity() const { return arity(); }                        // most arguments accepted; only a few natives (sort(arr, comparator)) take a range
    virtual Value call(Interpreter& interpreter, std::vector<Value>& arguments) = 0;
    virtual std::string toString() const = 0;                               // for printing, e.g. "<fn abc>"
    virtual ~Callable() = default;
};
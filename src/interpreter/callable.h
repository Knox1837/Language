// callable.h: the abstract interface every "thing you can call with ()" implements. 
// allows for addition of new native callable functions 
#pragma once
#include <vector>
#include <string>
#include "value.h"

class Interpreter; // forward declared to avoid a circular include with interpreter.h

class Callable {
public:
    virtual int arity() const = 0;                                          // expected argument count
    virtual Value call(Interpreter& interpreter, std::vector<Value>& arguments) = 0;
    virtual std::string toString() const = 0;                               // for printing, e.g. "<fn abc>"
    virtual ~Callable() = default;
};
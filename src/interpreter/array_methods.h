#pragma once
// array_methods.h: resolves obj.method for an array
// Returns a NativeFunction that's already bound to that specific array instance so arr.push(x) only needs to pass x, not arr and x). 
#include <memory>
#include "value.h"
#include "../lexer/token.h"

class ArrayObject;

// Throws RuntimeError if `name` isn't a known array method.
Value getArrayMethod(std::shared_ptr<ArrayObject> array, const Token& name);
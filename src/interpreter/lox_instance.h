// lox_instance.h — a runtime object created by calling a class
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "value.h"
#include "../lexer/token.h"

class LoxClass; // forward declared to avoid a circular include with lox_class.h

class LoxInstance : public std::enable_shared_from_this<LoxInstance> {
public:
    explicit LoxInstance(std::shared_ptr<LoxClass> klass);

    // obj.name— checks fields first, then falls back to a method on the class (bound to this instance so `this` works inside it). 
    // Throws RuntimeError if neither a field nor a method with that name exists.
    Value get(const Token& name);

    // obj.name = value — always writes/creates a field on the instance
    // itself (this language has no fixed field list — fields are created
    // on first assignment, same as Python's self.x = ... in __init__).
    void set(const Token& name, const Value& value);

    std::string toString() const;

private:
    std::shared_ptr<LoxClass> klass;
    std::unordered_map<std::string, Value> fields;
};
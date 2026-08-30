#pragma once
// environment.h: declares Environment, which stores variable bindingsfor one scope (global scope, or one block/function scope) and links to its enclosing scope so lookups can walk outward when a name isn'tfound locally
// allows for nesting scopes, e.g. a function defined inside another function can access the outer function's variables

#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include "../lexer/token.h"
#include "value.h"

// Thrown for runtime errors tied to a specific token (undefined variable, type mismatch in an operator, etc.) so the top-level runner can report "[line N] ...".
struct RuntimeError : std::runtime_error {
    Token token;
    RuntimeError(Token token, const std::string& message)
        : std::runtime_error(message), token(std::move(token)) {}
};

class Environment {
public:
    Environment() = default;
    explicit Environment(std::shared_ptr<Environment> enclosing);

    void define(const std::string& name, const Value& value); // create/overwrite a binding in THIS scope
    Value get(const Token& name);                              // look up a variable, walking outward if needed
    void assign(const Token& name, const Value& value);        // assign to an EXISTING variable, walking outward

private:
    std::unordered_map<std::string, Value> values;
    std::shared_ptr<Environment> enclosing; // nullptr for the global scope
};
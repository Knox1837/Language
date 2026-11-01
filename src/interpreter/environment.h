#pragma once
// environment.h: declares Environment, which stores variable bindingsfor one scope (global scope, or one block/function scope) and links to its enclosing scope so lookups can walk outward when a name isn'tfound locally
// allows for nesting scopes, e.g. a function defined inside another function can access the outer function's variables

#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <stdexcept>
#include "../lexer/token.h"
#include "value.h"

// Thrown for runtime errors tied to a specific token (undefined variable, type mismatch in an operator, etc.) so the top-level runner can report "[line N] ...".
struct RuntimeError : std::runtime_error {
    Token token;
    // Set only by error(value): the value itself, so a `catch` gets back exactly what was thrown. A failure raised by the language or a built-in has none, and its `catch` receives the message string.
    std::optional<Value> thrown;
    RuntimeError(Token token, const std::string& message)
        : std::runtime_error(message), token(std::move(token)) {}
};

// error(value) as a RuntimeError: the message (what an uncaught error prints) is the value's printed form. A named function, not a second constructor, because a string literal would convert to Value (as a bool!) as readily as to std::string and make every `RuntimeError(token, "...")` ambiguous.
inline RuntimeError makeThrownError(Token token, const Value& value) {
    RuntimeError error(std::move(token), stringifyValue(value));
    error.thrown = value;
    return error;
}

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
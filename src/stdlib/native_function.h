// native_function.h: wraps a plain C++ function (usually a lambda) as a Callable, so it can be defined into the global environment 
// Allows calling exactly like a user-defined `def` function. This is the one piece of plumbing every stdlib file builds on.
#pragma once
#include <functional>
#include <string>
#include "../interpreter/callable.h"
#include "../interpreter/environment.h" // for RuntimeError

// Native functions don't have a real source-code Token to attach to a RuntimeError (they're not sitting inside an AST node)
// so this builds  a synthetic one carrying just the function's name and a placeholder line number, for consistent, readable error messages.
inline RuntimeError nativeError(const std::string& functionName, const std::string& message) {
    Token synthetic(TokenType::IDENTIFIER, functionName, 0);
    return RuntimeError(synthetic, message);
}

class NativeFunction : public Callable {
public:
    using Fn = std::function<Value(Interpreter&, std::vector<Value>&)>;

    NativeFunction(std::string name, int arity, Fn fn);

    int arity() const override;
    Value call(Interpreter& interpreter, std::vector<Value>& arguments) override;
    std::string toString() const override;

private:
    std::string name;
    int arityCount;
    Fn fn;
};
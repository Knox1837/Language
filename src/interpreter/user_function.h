// user_function.h: wraps a `def name(...) { ... }` declaration into a callable runtime value
#pragma once
#include <memory>
#include "callable.h"
#include "environment.h"
#include "../ast/stmt.h"

class UserFunction : public Callable {
public:
    UserFunction(FunctionStmt* declaration, std::shared_ptr<Environment> closure);

    int arity() const override;
    Value call(Interpreter& interpreter, std::vector<Value>& arguments) override;
    std::string toString() const override;

private:
    FunctionStmt* declaration; // see FunctionStmt's comment in stmt.h re: lifetime
    std::shared_ptr<Environment> closure; // the scope active where this function was defined
};
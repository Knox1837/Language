// user_function.h: wraps a `def name(...) { ... }` declaration into a callable runtime value
#pragma once
#include <memory>
#include "callable.h"
#include "environment.h"
#include "../ast/stmt.h"

class LoxInstance; // forward declared for bind()

class UserFunction : public Callable, public std::enable_shared_from_this<UserFunction> {
public:
    UserFunction(FunctionStmt* declaration, std::shared_ptr<Environment> closure, bool isInitializer = false);

    int arity() const override;
    Value call(Interpreter& interpreter, std::vector<Value>& arguments) override;
    std::string toString() const override;

    // Produces a copy of this function whose closure has `this` bound to the given instance
    // used when a method is looked up on an instance (obj.method), so the method body can reference `this`.
    std::shared_ptr<UserFunction> bind(std::shared_ptr<LoxInstance> instance);

private:
    FunctionStmt* declaration; // see FunctionStmt's comment in stmt.h re: lifetime
    std::shared_ptr<Environment> closure; // the scope active where this function was defined
    bool isInitializer; // true for a class's init() method — always returns `this`, even on bare "return;"
};
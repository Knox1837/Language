// lox_class.h — the runtime representation of a `class Name { ... }` declaration.
// A LoxClass IS a Callable: calling it (Counter()) is what constructs a new instance and runs init() if one is defined.
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "callable.h"
#include "user_function.h"

class LoxClass : public Callable, public std::enable_shared_from_this<LoxClass> {
public:
    std::string name;

    LoxClass(std::string name, std::unordered_map<std::string, std::shared_ptr<UserFunction>> methods);
    // Looks up a method by name on this class. Returns nullptr if not found
    std::shared_ptr<UserFunction> findMethod(const std::string& methodName);

    int arity() const override;   // arity of init() if defined, else 0
    Value call(Interpreter& interpreter, std::vector<Value>& arguments) override; // constructs an instance
    std::string toString() const override;

private:
    std::unordered_map<std::string, std::shared_ptr<UserFunction>> methods;
};
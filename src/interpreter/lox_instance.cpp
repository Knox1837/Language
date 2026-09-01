// lox_instance.cpp — property get/set on a runtime object.
#include "lox_instance.h"
#include "lox_class.h"
#include "environment.h" // for RuntimeError

LoxInstance::LoxInstance(std::shared_ptr<LoxClass> klass) : klass(std::move(klass)) {}

Value LoxInstance::get(const Token& name) {
    auto it = fields.find(name.lexeme);
    if (it != fields.end()) {
        return it->second;
    }

    auto method = klass->findMethod(name.lexeme);
    if (method) {
        return method->bind(shared_from_this());
    }

    throw RuntimeError(name, "Undefined property '" + name.lexeme + "'.");
}

void LoxInstance::set(const Token& name, const Value& value) {
    fields[name.lexeme] = value; // fields are created on first assignment — no fixed field list
}

std::string LoxInstance::toString() const {
    return "<instance of " + klass->name + ">";
}
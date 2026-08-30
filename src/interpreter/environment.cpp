// environment.cpp: implements variable define/get/assign 
#include "environment.h"

Environment::Environment(std::shared_ptr<Environment> enclosing)
    : enclosing(std::move(enclosing)) {}

void Environment::define(const std::string& name, const Value& value) {
    values[name] = value; // allows redefinition with "var x = 1; var x = 2;"
}

Value Environment::get(const Token& name) {
    auto it = values.find(name.lexeme);
    if (it != values.end()) {
        return it->second;
    }

    if (enclosing) {
        return enclosing->get(name); // not found here — try the parent scope
    }

    throw RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
}

void Environment::assign(const Token& name, const Value& value) {
    auto it = values.find(name.lexeme);
    if (it != values.end()) {
        it->second = value;
        return;
    }

    if (enclosing) {
        enclosing->assign(name, value); // not declared here — try the parent scope
        return;
    }

    throw RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
}
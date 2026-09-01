// lox_class.cpp: implements calling a class
#include "lox_class.h"
#include "lox_instance.h"

LoxClass::LoxClass(std::string name, std::unordered_map<std::string, std::shared_ptr<UserFunction>> methods)
    : name(std::move(name)), methods(std::move(methods)) {}

std::shared_ptr<UserFunction> LoxClass::findMethod(const std::string& methodName) {
    auto it = methods.find(methodName);
    if (it != methods.end()) return it->second;
    return nullptr;
}

int LoxClass::arity() const {
    // const_cast is safe here: findMethod only reads the methods map.
    auto* self = const_cast<LoxClass*>(this);
    auto initializer = self->findMethod("init");
    return initializer ? initializer->arity() : 0;
}

Value LoxClass::call(Interpreter& interpreter, std::vector<Value>& arguments) {
    auto instance = std::make_shared<LoxInstance>(shared_from_this());

    auto initializer = findMethod("init");
    if (initializer) {
        initializer->bind(instance)->call(interpreter, arguments);
    }

    return instance;
}

std::string LoxClass::toString() const {
    return "<class " + name + ">";
}
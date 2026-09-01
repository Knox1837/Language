// user_function.cpp — implements calling a user-defined function

#include "user_function.h"
#include "interpreter.h"
#include "return_exception.h"
#include "lox_instance.h"

UserFunction::UserFunction(FunctionStmt* declaration, std::shared_ptr<Environment> closure, bool isInitializer)
    : declaration(declaration), closure(std::move(closure)), isInitializer(isInitializer) {}

int UserFunction::arity() const {
    return static_cast<int>(declaration->params.size());
}

std::shared_ptr<UserFunction> UserFunction::bind(std::shared_ptr<LoxInstance> instance) {
    // A fresh scope, one level above the method's own parameter scope, chained to the ORIGINAL closure so the method still sees whatever
    // it could already see (e.g. globals) — with `this` added on top.
    auto env = std::make_shared<Environment>(closure);
    env->define("this", Value{instance});
    return std::make_shared<UserFunction>(declaration, env, isInitializer);
}

Value UserFunction::call(Interpreter& interpreter, std::vector<Value>& arguments) {
    // New scope for this call, chained to the closure (where the function
    // was DEFINED / bound), not the caller's environment.
    auto callEnv = std::make_shared<Environment>(closure);

    for (size_t i = 0; i < declaration->params.size(); i++) {
        callEnv->define(declaration->params[i].lexeme, arguments[i]);
    }

    if (isInitializer) {
        // init() always returns `this`, regardless of what (if anything)
        // it explicitly returns — matches Lox's / Python's __init__ convention that a constructor's return value isn't used.
        Token thisToken(TokenType::THIS, "this", declaration->name.line);
        try {
            interpreter.executeBlock(declaration->body, callEnv);
        } catch (ReturnException&) {
            // fall through: still return `this` below, ignoring any returned value
        }
        return closure->get(thisToken);
    }

    try {
        interpreter.executeBlock(declaration->body, callEnv);
    } catch (ReturnException& returnValue) {
        return returnValue.value;
    }

    return std::monostate{}; // no explicit return -> nil, like Python's implicit None
}

std::string UserFunction::toString() const {
    return "<fn " + declaration->name.lexeme + ">";
}
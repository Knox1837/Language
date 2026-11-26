// user_function.cpp: implements calling a user-defined function

#include "user_function.h"
#include "interpreter.h"
#include "return_exception.h"
#include "lox_instance.h"
#include "call_stack.h"
#include "../util/trace.h"

UserFunction::UserFunction(FunctionStmt* declaration, std::shared_ptr<Environment> closure, bool isInitializer)
    : declaration(declaration), closure(std::move(closure)), isInitializer(isInitializer) {}

int UserFunction::arity() const {
    return declaration->requiredCount();
}

int UserFunction::maxArity() const {
    return static_cast<int>(declaration->params.size());
}

std::shared_ptr<UserFunction> UserFunction::bind(std::shared_ptr<LoxInstance> instance) {
    // A fresh scope, one level above the method's own parameter scope, chained to the ORIGINAL closure so the method still sees whatever it could already see (e.g. globals) - with `this` added on top.
    auto env = std::make_shared<Environment>(closure);
    env->define("this", Value{instance});
    return std::make_shared<UserFunction>(declaration, env, isInitializer);
}

Value UserFunction::call(Interpreter& interpreter, std::vector<Value>& arguments) {
    // A frame for stack traces: popped when this call ends normally (or by `return`), left on the stack when a runtime error passes through (see call_stack.h).
    CallFrameScope frame(traceFunctionLabel(declaration->name.lexeme));
    try {
        return run(interpreter, arguments);
    } catch (const RuntimeError&) {
        frame.keep();
        throw;
    }
}

Value UserFunction::run(Interpreter& interpreter, std::vector<Value>& arguments) {
    // New scope for this call, chained to the closure (where the function was DEFINED / bound), not the caller's environment.
    auto callEnv = std::make_shared<Environment>(closure);

    // A parameter whose argument was left out gets its default, evaluated now, in this call's own scope: it can use the parameters before it (and anything the function can see), and each call gets a fresh value (a default of [] is a new array every time).
    for (size_t i = 0; i < declaration->params.size(); i++) {
        if (i < arguments.size()) {
            callEnv->define(declaration->params[i].lexeme, arguments[i]);
        } else {
            // (The parser reports `def f(a = 1, b)` as an error, but the tree-walker still runs the rest of the file, so a missing default must not crash: it is nil.)
            Value fallback = declaration->defaults[i] ? interpreter.evaluateIn(*declaration->defaults[i], callEnv) : Value{std::monostate{}};
            callEnv->define(declaration->params[i].lexeme, fallback);
        }
    }

    if (isInitializer) {
        // init() always returns `this`, regardless of what (if anything) it explicitly returns - matches Lox's / Python's __init__ convention that a constructor's return value isn't used.
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
    if (declaration->name.lexeme.empty()) return "<fn>"; // an anonymous function (def(x) { ... })
    return "<fn " + declaration->name.lexeme + ">";
}
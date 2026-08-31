// user_function.cpp — implements calling a user-defined function

#include "user_function.h"
#include "interpreter.h"
#include "return_exception.h"

UserFunction::UserFunction(FunctionStmt* declaration, std::shared_ptr<Environment> closure)
    : declaration(declaration), closure(std::move(closure)) {}

int UserFunction::arity() const {
    return static_cast<int>(declaration->params.size());
}

Value UserFunction::call(Interpreter& interpreter, std::vector<Value>& arguments) {
    // New scope for this call, chained to the closure (where the function
    // was DEFINED), not the caller's environment — this is what makes
    // closures capture the right variables regardless of who calls them.
    auto callEnv = std::make_shared<Environment>(closure);

    for (size_t i = 0; i < declaration->params.size(); i++) {
        callEnv->define(declaration->params[i].lexeme, arguments[i]);
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
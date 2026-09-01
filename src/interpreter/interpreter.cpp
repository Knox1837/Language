// interpreter.cpp: walks the AST and actually executes it. Expressions are evaluated to a Value; statements are executed for their side effects (printing, defining variables, branching, looping).
#include "interpreter.h"
#include "user_function.h"
#include "return_exception.h"
#include "lox_class.h"
#include "lox_instance.h"
#include <iostream>
#include <cmath>

Interpreter::Interpreter() {
    environment = std::make_shared<Environment>(); // the global scope
}

void Interpreter::interpret(const std::vector<StmtPtr>& statements) {
    try {
        for (const auto& stmt : statements) {
            if (!stmt) continue; // a failed declaration() during parsing leaves a null entry
            execute(*stmt);
        }
    } catch (const RuntimeError& error) {
        std::cerr << "[line " << error.token.line << "] Runtime error: " << error.what() << "\n";
    }
}

Value Interpreter::evaluate(Expr& expr) {
    expr.accept(*this);
    return result;
}

void Interpreter::execute(Stmt& stmt) {
    stmt.accept(*this);
}

void Interpreter::executeBlock(const std::vector<StmtPtr>& statements, std::shared_ptr<Environment> newEnv) {
    std::shared_ptr<Environment> previous = environment;
    environment = std::move(newEnv);
    try {
        for (const auto& stmt : statements) {
            if (!stmt) continue;
            execute(*stmt);
        }
    } catch (...) {
        environment = previous; // restore scope even if a runtime error/return propagates
        throw;
    }
    environment = previous;
}

// expressions

void Interpreter::visitLiteralExpr(Literal& expr) {
    result = fromLiteral(expr.value);
}

void Interpreter::visitGroupingExpr(Grouping& expr) {
    result = evaluate(*expr.expression);
}

void Interpreter::visitUnaryExpr(Unary& expr) {
    Value right = evaluate(*expr.right);

    switch (expr.op.type) {
        case TokenType::MINUS:
            checkNumberOperand(expr.op, right);
            result = -std::get<double>(right);
            return;
        case TokenType::BANG:
            result = !isTruthy(right);
            return;
        default:
            break; // unreachable if the parser only ever builds valid unary ops
    }
}

void Interpreter::visitBinaryExpr(Binary& expr) {
    Value left = evaluate(*expr.left);
    Value right = evaluate(*expr.right);

    switch (expr.op.type) {
        case TokenType::MINUS:
            checkNumberOperands(expr.op, left, right);
            result = std::get<double>(left) - std::get<double>(right);
            return;
        case TokenType::SLASH:
            checkNumberOperands(expr.op, left, right);
            if (std::get<double>(right) == 0.0) {
                throw RuntimeError(expr.op, "Division by zero.");
            }
            result = std::get<double>(left) / std::get<double>(right);
            return;
        case TokenType::STAR:
            checkNumberOperands(expr.op, left, right);
            result = std::get<double>(left) * std::get<double>(right);
            return;
        case TokenType::PLUS:
            // '+' overloads: number+number adds, string+string concatenates
            if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right)) {
                result = std::get<double>(left) + std::get<double>(right);
                return;
            }
            if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
                result = std::get<std::string>(left) + std::get<std::string>(right);
                return;
            }
            throw RuntimeError(expr.op, "Operands must be two numbers or two strings.");
        case TokenType::GREATER:
            checkNumberOperands(expr.op, left, right);
            result = std::get<double>(left) > std::get<double>(right);
            return;
        case TokenType::GREATER_EQUAL:
            checkNumberOperands(expr.op, left, right);
            result = std::get<double>(left) >= std::get<double>(right);
            return;
        case TokenType::LESS:
            checkNumberOperands(expr.op, left, right);
            result = std::get<double>(left) < std::get<double>(right);
            return;
        case TokenType::LESS_EQUAL:
            checkNumberOperands(expr.op, left, right);
            result = std::get<double>(left) <= std::get<double>(right);
            return;
        case TokenType::EQUAL_EQUAL:
            result = isEqual(left, right);
            return;
        case TokenType::BANG_EQUAL:
            result = !isEqual(left, right);
            return;
        default:
            break; // unreachable if the parser only ever builds valid binary ops
    }
}

void Interpreter::visitVariableExpr(Variable& expr) {
    result = environment->get(expr.name);
}

void Interpreter::visitAssignExpr(Assign& expr) {
    Value value = evaluate(*expr.value);
    environment->assign(expr.name, value);
    result = value; // assignment is itself an expression: x = (y = 5) works
}

void Interpreter::visitLogicalExpr(Logical& expr) {
    Value left = evaluate(*expr.left);

    // Short-circuit: only evaluate the right side if actually needed
    if (expr.op.type == TokenType::OR) {
        if (isTruthy(left)) { result = left; return; }
    } else { // AND
        if (!isTruthy(left)) { result = left; return; }
    }

    result = evaluate(*expr.right);
}

void Interpreter::visitCallExpr(Call& expr) {
    Value callee = evaluate(*expr.callee);

    std::vector<Value> arguments;
    arguments.reserve(expr.arguments.size());
    for (auto& argExpr : expr.arguments) {
        arguments.push_back(evaluate(*argExpr));
    }

    if (!std::holds_alternative<std::shared_ptr<Callable>>(callee)) {
        throw RuntimeError(expr.paren, "Can only call functions.");
    }

    auto function = std::get<std::shared_ptr<Callable>>(callee);
    if (static_cast<int>(arguments.size()) != function->arity()) {
        throw RuntimeError(expr.paren,
            "Expected " + std::to_string(function->arity()) +
            " arguments but got " + std::to_string(arguments.size()) + ".");
    }

    result = function->call(*this, arguments);
}

void Interpreter::visitGetExpr(Get& expr) {
    Value object = evaluate(*expr.object);
    if (!std::holds_alternative<std::shared_ptr<LoxInstance>>(object)) {
        throw RuntimeError(expr.name, "Only instances have properties.");
    }
    result = std::get<std::shared_ptr<LoxInstance>>(object)->get(expr.name);
}

void Interpreter::visitSetExpr(Set& expr) {
    Value object = evaluate(*expr.object);
    if (!std::holds_alternative<std::shared_ptr<LoxInstance>>(object)) {
        throw RuntimeError(expr.name, "Only instances have fields.");
    }
    Value value = evaluate(*expr.value);
    std::get<std::shared_ptr<LoxInstance>>(object)->set(expr.name, value);
    result = value; // "a.b = c" is itself an expression, same as plain assignment
}

void Interpreter::visitThisExpr(This& expr) {
    result = environment->get(expr.keyword);
}

// statements

void Interpreter::visitExpressionStmt(ExpressionStmt& stmt) {
    evaluate(*stmt.expression);
}

void Interpreter::visitPrintStmt(PrintStmt& stmt) {
    Value value = evaluate(*stmt.expression);
    std::cout << stringifyValue(value) << "\n";
}

void Interpreter::visitVarStmt(VarStmt& stmt) {
    Value value = std::monostate{}; // "var x;" with no initializer defaults to nil
    if (stmt.initializer) {
        value = evaluate(*stmt.initializer);
    }
    environment->define(stmt.name.lexeme, value);
}

void Interpreter::visitBlockStmt(BlockStmt& stmt) {
    executeBlock(stmt.statements, std::make_shared<Environment>(environment));
}

void Interpreter::visitIfStmt(IfStmt& stmt) {
    if (isTruthy(evaluate(*stmt.condition))) {
        execute(*stmt.thenBranch);
    } else if (stmt.elseBranch) {
        execute(*stmt.elseBranch);
    }
}

void Interpreter::visitWhileStmt(WhileStmt& stmt) {
    while (isTruthy(evaluate(*stmt.condition))) {
        execute(*stmt.body);
    }
}

void Interpreter::visitFunctionStmt(FunctionStmt& stmt) {
    // Capture the CURRENT environment as the closure — this is what lets
    // the function later see variables from its defining scope even if
    // called from somewhere else entirely.
    auto function = std::make_shared<UserFunction>(&stmt, environment);
    environment->define(stmt.name.lexeme, function);
}

void Interpreter::visitReturnStmt(ReturnStmt& stmt) {
    Value value = std::monostate{};
    if (stmt.value) {
        value = evaluate(*stmt.value);
    }
    throw ReturnException(value); // unwinds back to UserFunction::call()
}

void Interpreter::visitClassStmt(ClassStmt& stmt) {
    // Two-step define/assign (like Lox) so a method body could in principle reference the class's own name
    environment->define(stmt.name.lexeme, std::monostate{});

    std::unordered_map<std::string, std::shared_ptr<UserFunction>> methods;
    for (auto& methodDecl : stmt.methods) {
        bool isInitializer = (methodDecl->name.lexeme == "init");
        auto function = std::make_shared<UserFunction>(methodDecl.get(), environment, isInitializer);
        methods[methodDecl->name.lexeme] = function;
    }

    auto klass = std::make_shared<LoxClass>(stmt.name.lexeme, std::move(methods));
    environment->assign(stmt.name, Value{klass});
}

// helpers

void Interpreter::checkNumberOperand(const Token& op, const Value& operand) {
    if (std::holds_alternative<double>(operand)) return;
    throw RuntimeError(op, "Operand must be a number.");
}

void Interpreter::checkNumberOperands(const Token& op, const Value& left, const Value& right) {
    if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right)) return;
    throw RuntimeError(op, "Operands must be numbers.");
}

bool Interpreter::isEqual(const Value& a, const Value& b) {
    return a == b; // std::variant provides value equality when held types are comparable
}
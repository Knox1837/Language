// interpreter.cpp: walks the AST and actually executes it. Expressions are evaluated to a Value; statements are executed for their side effects (printing, defining variables, branching, looping).

#include "interpreter.h"
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
        environment = previous; // restore scope even if a runtime error/exception propagates
        throw;
    }
    environment = previous;
}

// expressions

void Interpreter::visitLiteralExpr(Literal& expr) {
    result = expr.value;
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

// helper

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
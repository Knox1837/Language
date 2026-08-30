// expr.h — defines every kind of EXPRESSION node in the AST (things that produce a value: 1 + 2, x, "hello", (a == b), x = 5, etc).
#pragma once
#include <memory>
#include <string>
#include <variant>
#include "../lexer/token.h"

struct Binary;
struct Grouping;
struct Literal;
struct Unary;
struct Variable;
struct Assign;
struct Logical;

// Runtime literal value: number, string, bool, or nil (monostate)
using LiteralValue = std::variant<std::monostate, double, bool, std::string>;

// Visitor interface — one visit method per expression type.
struct ExprVisitor {
    virtual void visitBinaryExpr(Binary& expr) = 0;
    virtual void visitGroupingExpr(Grouping& expr) = 0;
    virtual void visitLiteralExpr(Literal& expr) = 0;
    virtual void visitUnaryExpr(Unary& expr) = 0;
    virtual void visitVariableExpr(Variable& expr) = 0;
    virtual void visitAssignExpr(Assign& expr) = 0;
    virtual void visitLogicalExpr(Logical& expr) = 0;
    virtual ~ExprVisitor() = default;
};

// Base class every expression node inherits from.
struct Expr {
    virtual void accept(ExprVisitor& visitor) = 0;
    virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

// left OP right   e.g.  a + b,  x == y,  n < 10
struct Binary : Expr {
    ExprPtr left;
    Token op;
    ExprPtr right;
    Binary(ExprPtr left, Token op, ExprPtr right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitBinaryExpr(*this); }
};

// ( expression )  — just wraps an inner expr to override precedence
struct Grouping : Expr {
    ExprPtr expression;
    explicit Grouping(ExprPtr expression) : expression(std::move(expression)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitGroupingExpr(*this); }
};

// A raw literal value straight from source: 123, "hi", true, nil
struct Literal : Expr {
    LiteralValue value;
    explicit Literal(LiteralValue value) : value(std::move(value)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitLiteralExpr(*this); }
};

// OP right   e.g.  -x,  !flag
struct Unary : Expr {
    Token op;
    ExprPtr right;
    Unary(Token op, ExprPtr right) : op(std::move(op)), right(std::move(right)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitUnaryExpr(*this); }
};

// A reference to a variable by name: x
struct Variable : Expr {
    Token name;
    explicit Variable(Token name) : name(std::move(name)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitVariableExpr(*this); }
};

// name = value   — assigning to an existing variable
struct Assign : Expr {
    Token name;
    ExprPtr value;
    Assign(Token name, ExprPtr value) : name(std::move(name)), value(std::move(value)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitAssignExpr(*this); }
};

// left AND right / left OR right — short-circuiting, so kept separate from Binary
struct Logical : Expr {
    ExprPtr left;
    Token op;
    ExprPtr right;
    Logical(ExprPtr left, Token op, ExprPtr right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitLogicalExpr(*this); }
};
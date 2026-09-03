// expr.h: defines every kind of EXPRESSION node in the AST (things that produce a value: 1 + 2, x, "hello", (a == b), x = 5, etc).
#pragma once
// expr.h - defines every kind of EXPRESSION node in the AST (things that
// produce a value: 1 + 2, x, "hello", (a == b), x = 5, etc).
//
// Uses the Visitor pattern: each node type implements accept(), and any
// new operation (interpreter, AST printer, compiler) implements
// ExprVisitor without touching these node classes again. Because C++
// doesn't allow virtual template methods, the visitor's visit() methods
// return void - a visitor stores its result internally and the caller
// reads it back out after accept() runs (see ASTPrinter for an example).

#include <memory>
#include <string>
#include <variant>
#include <vector>
#include "../lexer/token.h"

struct Binary;
struct Grouping;
struct Literal;
struct Unary;
struct Variable;
struct Assign;
struct Logical;
struct Call;
struct Get;
struct Set;
struct This;
struct Super;
struct ArrayLiteral;
struct Index;
struct IndexSet;

// Runtime literal value: number, string, bool, or nil (monostate)
using LiteralValue = std::variant<std::monostate, double, bool, std::string>;

// Visitor interface - one visit method per expression type.
struct ExprVisitor {
    virtual void visitBinaryExpr(Binary& expr) = 0;
    virtual void visitGroupingExpr(Grouping& expr) = 0;
    virtual void visitLiteralExpr(Literal& expr) = 0;
    virtual void visitUnaryExpr(Unary& expr) = 0;
    virtual void visitVariableExpr(Variable& expr) = 0;
    virtual void visitAssignExpr(Assign& expr) = 0;
    virtual void visitLogicalExpr(Logical& expr) = 0;
    virtual void visitCallExpr(Call& expr) = 0;
    virtual void visitGetExpr(Get& expr) = 0;
    virtual void visitSetExpr(Set& expr) = 0;
    virtual void visitThisExpr(This& expr) = 0;
    virtual void visitSuperExpr(Super& expr) = 0;
    virtual void visitArrayLiteralExpr(ArrayLiteral& expr) = 0;
    virtual void visitIndexExpr(Index& expr) = 0;
    virtual void visitIndexSetExpr(IndexSet& expr) = 0;
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

// ( expression )  - just wraps an inner expr to override precedence
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

// name = value   - assigning to an existing variable
struct Assign : Expr {
    Token name;
    ExprPtr value;
    Assign(Token name, ExprPtr value) : name(std::move(name)), value(std::move(value)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitAssignExpr(*this); }
};

// left AND right / left OR right - short-circuiting, so kept separate from Binary
struct Logical : Expr {
    ExprPtr left;
    Token op;
    ExprPtr right;
    Logical(ExprPtr left, Token op, ExprPtr right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitLogicalExpr(*this); }
};

// callee(arg1, arg2, ...)   e.g.  abc(1, 2)
// `paren` is the closing ')' token, kept only so runtime errors (like "wrong number of arguments") can report a line number.
struct Call : Expr {
    ExprPtr callee;
    Token paren;
    std::vector<ExprPtr> arguments;
    Call(ExprPtr callee, Token paren, std::vector<ExprPtr> arguments)
        : callee(std::move(callee)), paren(std::move(paren)), arguments(std::move(arguments)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitCallExpr(*this); }
};

// object.name   - reads a property (field OR method) off an instance
struct Get : Expr {
    ExprPtr object;
    Token name;
    Get(ExprPtr object, Token name) : object(std::move(object)), name(std::move(name)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitGetExpr(*this); }
};

// object.name = value   - writes a field on an instance
struct Set : Expr {
    ExprPtr object;
    Token name;
    ExprPtr value;
    Set(ExprPtr object, Token name, ExprPtr value)
        : object(std::move(object)), name(std::move(name)), value(std::move(value)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitSetExpr(*this); }
};

// the "this" keyword inside a method body - refers to the current instance
struct This : Expr {
    Token keyword;
    explicit This(Token keyword) : keyword(std::move(keyword)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitThisExpr(*this); }
};

// super.method: calls a method from the superclass, bypassing any override in the current class. 
// `keyword` is the "super" token itself (kept for error line-numbers); `method` is the method name after the dot.
struct Super : Expr {
    Token keyword;
    Token method;
    Super(Token keyword, Token method) : keyword(std::move(keyword)), method(std::move(method)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitSuperExpr(*this); }
};

// [expr1, expr2, ...] - an array literal
struct ArrayLiteral : Expr {
    std::vector<ExprPtr> elements;
    explicit ArrayLiteral(std::vector<ExprPtr> elements) : elements(std::move(elements)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitArrayLiteralExpr(*this); }
};

// object[index]: reads an element out of an array
// `bracket` is the '[' token, kept only for error line-numbers.
struct Index : Expr {
    ExprPtr object;
    Token bracket;
    ExprPtr indexExpr;
    Index(ExprPtr object, Token bracket, ExprPtr indexExpr)
        : object(std::move(object)), bracket(std::move(bracket)), indexExpr(std::move(indexExpr)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitIndexExpr(*this); }
};

// object[index] = value   - writes an element into an array
struct IndexSet : Expr {
    ExprPtr object;
    Token bracket;
    ExprPtr indexExpr;
    ExprPtr value;
    IndexSet(ExprPtr object, Token bracket, ExprPtr indexExpr, ExprPtr value)
        : object(std::move(object)), bracket(std::move(bracket)),
          indexExpr(std::move(indexExpr)), value(std::move(value)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitIndexSetExpr(*this); }
};
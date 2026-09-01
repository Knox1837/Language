// stmt.h: defines every kind of STATEMENT node in the AST (things that perform an action but don't themselves produce a value like var x = 1;
#pragma once
#include <memory>
#include <vector>
#include "expr.h"

struct ExpressionStmt;
struct PrintStmt;
struct VarStmt;
struct BlockStmt;
struct IfStmt;
struct WhileStmt;
struct FunctionStmt;
struct ReturnStmt;
struct ClassStmt;

struct StmtVisitor {
    virtual void visitExpressionStmt(ExpressionStmt& stmt) = 0;
    virtual void visitPrintStmt(PrintStmt& stmt) = 0;
    virtual void visitVarStmt(VarStmt& stmt) = 0;
    virtual void visitBlockStmt(BlockStmt& stmt) = 0;
    virtual void visitIfStmt(IfStmt& stmt) = 0;
    virtual void visitWhileStmt(WhileStmt& stmt) = 0;
    virtual void visitFunctionStmt(FunctionStmt& stmt) = 0;
    virtual void visitReturnStmt(ReturnStmt& stmt) = 0;
    virtual void visitClassStmt(ClassStmt& stmt) = 0;
    virtual ~StmtVisitor() = default;
};

struct Stmt {
    virtual void accept(StmtVisitor& visitor) = 0;
    virtual ~Stmt() = default;
};

using StmtPtr = std::unique_ptr<Stmt>;

// A bare expression used as a statement, e.g. a function call: doThing();
struct ExpressionStmt : Stmt {
    ExprPtr expression;
    explicit ExpressionStmt(ExprPtr expression) : expression(std::move(expression)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitExpressionStmt(*this); }
};

// print expression;
struct PrintStmt : Stmt {
    ExprPtr expression;
    explicit PrintStmt(ExprPtr expression) : expression(std::move(expression)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitPrintStmt(*this); }
};

// var name = initializer;   (initializer may be null: "var x;")
struct VarStmt : Stmt {
    Token name;
    ExprPtr initializer; // nullptr if no initializer given
    VarStmt(Token name, ExprPtr initializer)
        : name(std::move(name)), initializer(std::move(initializer)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitVarStmt(*this); }
};

// { statement* }  — introduces a new variable scope
struct BlockStmt : Stmt {
    std::vector<StmtPtr> statements;
    explicit BlockStmt(std::vector<StmtPtr> statements) : statements(std::move(statements)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitBlockStmt(*this); }
};

// if (condition) thenBranch else elseBranch   (elseBranch may be null)
struct IfStmt : Stmt {
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch; // nullptr if no else clause
    IfStmt(ExprPtr condition, StmtPtr thenBranch, StmtPtr elseBranch)
        : condition(std::move(condition)), thenBranch(std::move(thenBranch)),
          elseBranch(std::move(elseBranch)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitIfStmt(*this); }
};

// while (condition) body
struct WhileStmt : Stmt {
    ExprPtr condition;
    StmtPtr body;
    WhileStmt(ExprPtr condition, StmtPtr body)
        : condition(std::move(condition)), body(std::move(body)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitWhileStmt(*this); }
};

// def name(param1, param2) { body }
struct FunctionStmt : Stmt {
    Token name;
    std::vector<Token> params;
    std::vector<StmtPtr> body;
    FunctionStmt(Token name, std::vector<Token> params, std::vector<StmtPtr> body)
        : name(std::move(name)), params(std::move(params)), body(std::move(body)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitFunctionStmt(*this); }
};

// return expression;   or just   return;  (value may be null -> returns nil)
struct ReturnStmt : Stmt {
    Token keyword; // the "return" token, kept for line-number reporting
    ExprPtr value; // nullptr if bare "return;"
    ReturnStmt(Token keyword, ExprPtr value)
        : keyword(std::move(keyword)), value(std::move(value)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitReturnStmt(*this); }
};

// class Name { method1(...) {...} method2(...) {...} }
struct ClassStmt : Stmt {
    Token name;
    std::vector<std::unique_ptr<FunctionStmt>> methods;
    ClassStmt(Token name, std::vector<std::unique_ptr<FunctionStmt>> methods)
        : name(std::move(name)), methods(std::move(methods)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitClassStmt(*this); }
};
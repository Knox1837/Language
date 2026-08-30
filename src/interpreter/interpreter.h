#pragma once
// interpreter.h: declares Interpreter, the tree-walking evaluator.
// Both ExprVisitor (to compute values) and StmtVisitor (to execute statements). Same "visitor stores result internally" pattern  as ASTPrinter

#include <memory>
#include <vector>
#include "../ast/expr.h"
#include "../ast/stmt.h"
#include "environment.h"
#include "value.h"

class Interpreter : public ExprVisitor, public StmtVisitor {
public:
    Interpreter();

    // Entry point: executes a whole program (list of top-level statements).
    // Catches RuntimeError internally and reports it, matching how a real
    // script runner behaves (one runtime error stops execution and prints it).
    void interpret(const std::vector<StmtPtr>& statements);

    // --- expression visitors: each computes a Value and stores it in `result` ---
    void visitBinaryExpr(Binary& expr) override;
    void visitGroupingExpr(Grouping& expr) override;
    void visitLiteralExpr(Literal& expr) override;
    void visitUnaryExpr(Unary& expr) override;
    void visitVariableExpr(Variable& expr) override;
    void visitAssignExpr(Assign& expr) override;
    void visitLogicalExpr(Logical& expr) override;

    // --- statement visitors: each performs an action (no return value) ---
    void visitExpressionStmt(ExpressionStmt& stmt) override;
    void visitPrintStmt(PrintStmt& stmt) override;
    void visitVarStmt(VarStmt& stmt) override;
    void visitBlockStmt(BlockStmt& stmt) override;
    void visitIfStmt(IfStmt& stmt) override;
    void visitWhileStmt(WhileStmt& stmt) override;

private:
    std::shared_ptr<Environment> environment; // current scope; starts as globals

    Value result; // scratch slot: evaluate() reads this after accept() runs

    Value evaluate(Expr& expr);   // helper: run accept() and return `result`
    void execute(Stmt& stmt);     // helper: run accept() for a statement
    void executeBlock(const std::vector<StmtPtr>& statements, std::shared_ptr<Environment> newEnv);

    // Type-checking helpers used by binary/unary operators
    static void checkNumberOperand(const Token& op, const Value& operand);
    static void checkNumberOperands(const Token& op, const Value& left, const Value& right);
    static bool isEqual(const Value& a, const Value& b);
};
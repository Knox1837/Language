// interpreter.h: declares Interpreter, the tree-walking evaluator.
// Both ExprVisitor (to compute values) and StmtVisitor (to execute statements). Same "visitor stores result internally" pattern  as ASTPrinter
#pragma once
#include <memory>
#include <vector>
#include "../ast/expr.h"
#include "../ast/stmt.h"
#include "environment.h"
#include "value.h"
#include "lox_class.h"
#include "lox_instance.h"

class ModuleLoader; // forward declared to avoid a circular include with module_loader.h

class Interpreter : public ExprVisitor, public StmtVisitor {
public:
    Interpreter();
    ~Interpreter(); // defined in interpreter.cpp, where ModuleLoader is a complete type (needed because unique_ptr<ModuleLoader>'s destructor requires the full definition, which this header only forward-declares)

    // Exposes the global scope so ModuleLoader can wrap it in a ModuleObject after running an imported file's top-level code.
    std::shared_ptr<Environment> getGlobalEnvironment() const { return environment; }

    // Called once by main.cpp after construction, with the directory the top-level script lives in 
    void setModuleBaseDir(const std::string& dir);

    // Used by ModuleLoader itself when running an imported file in a fresh Interpreter, so that the imported file's own imports resolve relative to its own directory rather than the importing script's directory.
    void setSharedModuleLoader(std::shared_ptr<ModuleLoader> loader);

    // Entry point: executes a whole program (list of top-level statements).
    // Catches RuntimeError internally and reports it, matching how a real script runner behaves (one runtime error stops execution and prints it).
    void interpret(const std::vector<StmtPtr>& statements);

    // Runs `statements` in a fresh scope chained to `newEnv`'s parent chain.
    // Public because UserFunction::call() needs it to run a function body
    // in a scope chained to the function's closure, not the caller's scope.
    void executeBlock(const std::vector<StmtPtr>& statements, std::shared_ptr<Environment> newEnv);

    // expression visitors: each computes a Value and stores it in `result`
    void visitBinaryExpr(Binary& expr) override;
    void visitGroupingExpr(Grouping& expr) override;
    void visitLiteralExpr(Literal& expr) override;
    void visitUnaryExpr(Unary& expr) override;
    void visitVariableExpr(Variable& expr) override;
    void visitAssignExpr(Assign& expr) override;
    void visitLogicalExpr(Logical& expr) override;
    void visitCallExpr(Call& expr) override;
    void visitGetExpr(Get& expr) override;
    void visitSetExpr(Set& expr) override;
    void visitThisExpr(This& expr) override;
    void visitSuperExpr(Super& expr) override;
    void visitArrayLiteralExpr(ArrayLiteral& expr) override;
    void visitIndexExpr(Index& expr) override;
    void visitIndexSetExpr(IndexSet& expr) override;
    void visitMapLiteralExpr(MapLiteral& expr) override;
    void visitCompoundSetExpr(CompoundSet& expr) override;
    void visitCompoundIndexSetExpr(CompoundIndexSet& expr) override;

    // statement visitors: each performs an action (no return value)
    void visitExpressionStmt(ExpressionStmt& stmt) override;
    void visitPrintStmt(PrintStmt& stmt) override;
    void visitVarStmt(VarStmt& stmt) override;
    void visitBlockStmt(BlockStmt& stmt) override;
    void visitIfStmt(IfStmt& stmt) override;
    void visitWhileStmt(WhileStmt& stmt) override;
    void visitFunctionStmt(FunctionStmt& stmt) override;
    void visitReturnStmt(ReturnStmt& stmt) override;
    void visitClassStmt(ClassStmt& stmt) override;
    void visitImportStmt(ImportStmt& stmt) override;

private:
    std::shared_ptr<Environment> environment; // current scope; starts as globals
    std::shared_ptr<ModuleLoader> moduleLoader; // shared across the whole import graph, not per-file

    Value result; // scratch slot: evaluate() reads this after accept() runs

    Value evaluate(Expr& expr);   // helper: run accept() and return `result`
    void execute(Stmt& stmt);     // helper: run accept() for a statement

    // Type-checking helpers used by binary/unary operators
    static void checkNumberOperand(const Token& op, const Value& operand);
    static void checkNumberOperands(const Token& op, const Value& left, const Value& right);
    static bool isEqual(const Value& a, const Value& b);
    static Value applyArithmeticOp(TokenType op, const Token& opToken, const Value& left, const Value& right);
};
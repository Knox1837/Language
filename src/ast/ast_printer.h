// ast_printer.h — a small ExprVisitor/StmtVisitor implementation that converts the AST back into a readable, parenthesized string
// e.g. "1 + 2 * 3" prints as "(+ 1 (* 2 3))". 
// to see the AST structure, not the original source code. Useful for debugging the parser.
#pragma once
#include <string>
#include <sstream>
#include "expr.h"
#include "stmt.h"

class ASTPrinter : public ExprVisitor, public StmtVisitor {
public:
    std::string print(Expr& expr);
    std::string print(const std::vector<StmtPtr>& statements);

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

    void visitExpressionStmt(ExpressionStmt& stmt) override;
    void visitPrintStmt(PrintStmt& stmt) override;
    void visitVarStmt(VarStmt& stmt) override;
    void visitBlockStmt(BlockStmt& stmt) override;
    void visitIfStmt(IfStmt& stmt) override;
    void visitWhileStmt(WhileStmt& stmt) override;
    void visitFunctionStmt(FunctionStmt& stmt) override;
    void visitReturnStmt(ReturnStmt& stmt) override;
    void visitClassStmt(ClassStmt& stmt) override;

private:
    std::string result; // visit methods write here; print() reads it back out

    // helper: wraps a name and any number of child exprs as "(name child1 child2 ...)"
    // Defined inline here (not in the .cpp) because template definitions must be visible at every call site
    template <typename... Exprs>
    std::string parenthesize(const std::string& name, Exprs&... exprs) {
        std::ostringstream out;
        out << "(" << name;
        (void)std::initializer_list<int>{(out << " " << print(exprs), 0)...};
        out << ")";
        return out.str();
    }
};
#include "ast_printer.h"

std::string ASTPrinter::print(Expr& expr) {
    expr.accept(*this);
    return result;
}

std::string ASTPrinter::print(const std::vector<StmtPtr>& statements) {
    std::ostringstream out;
    for (const auto& stmt : statements) {
        if (!stmt) continue; // a failed declaration() can leave a null entry
        stmt->accept(*this);
        out << result << "\n";
    }
    return out.str();
}

void ASTPrinter::visitBinaryExpr(Binary& expr) {
    result = parenthesize(expr.op.lexeme, *expr.left, *expr.right);
}

void ASTPrinter::visitGroupingExpr(Grouping& expr) {
    result = parenthesize("group", *expr.expression);
}

void ASTPrinter::visitLiteralExpr(Literal& expr) {
    std::visit([this](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        std::ostringstream out;
        if constexpr (std::is_same_v<T, std::monostate>) out << "nil";
        else if constexpr (std::is_same_v<T, bool>) out << (v ? "true" : "false");
        else if constexpr (std::is_same_v<T, double>) out << v;
        else out << v; // std::string
        result = out.str();
    }, expr.value);
}

void ASTPrinter::visitUnaryExpr(Unary& expr) {
    result = parenthesize(expr.op.lexeme, *expr.right);
}

void ASTPrinter::visitVariableExpr(Variable& expr) {
    result = expr.name.lexeme;
}

void ASTPrinter::visitAssignExpr(Assign& expr) {
    result = parenthesize("= " + expr.name.lexeme, *expr.value);
}

void ASTPrinter::visitLogicalExpr(Logical& expr) {
    result = parenthesize(expr.op.lexeme, *expr.left, *expr.right);
}

void ASTPrinter::visitExpressionStmt(ExpressionStmt& stmt) {
    result = parenthesize("expr", *stmt.expression);
}

void ASTPrinter::visitPrintStmt(PrintStmt& stmt) {
    result = parenthesize("print", *stmt.expression);
}

void ASTPrinter::visitVarStmt(VarStmt& stmt) {
    if (stmt.initializer) {
        result = parenthesize("var " + stmt.name.lexeme, *stmt.initializer);
    } else {
        result = "(var " + stmt.name.lexeme + ")";
    }
}

void ASTPrinter::visitBlockStmt(BlockStmt& stmt) {
    std::ostringstream out;
    out << "(block ";
    for (auto& s : stmt.statements) {
        if (!s) continue;
        s->accept(*this);
        out << result << " ";
    }
    out << ")";
    result = out.str();
}

void ASTPrinter::visitIfStmt(IfStmt& stmt) {
    std::ostringstream out;
    out << "(if " << print(*stmt.condition);
    stmt.thenBranch->accept(*this);
    out << " " << result;
    if (stmt.elseBranch) {
        stmt.elseBranch->accept(*this);
        out << " " << result;
    }
    out << ")";
    result = out.str();
}

void ASTPrinter::visitWhileStmt(WhileStmt& stmt) {
    std::ostringstream out;
    out << "(while " << print(*stmt.condition);
    stmt.body->accept(*this);
    out << " " << result << ")";
    result = out.str();
}
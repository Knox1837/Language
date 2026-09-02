// ast_printer.cpp: implements ASTPrinter, a visitor that prints the AST in a Lisp-like parenthesized format
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

void ASTPrinter::visitCallExpr(Call& expr) {
    std::ostringstream out;
    out << "(call " << print(*expr.callee);
    for (auto& arg : expr.arguments) {
        out << " " << print(*arg);
    }
    out << ")";
    result = out.str();
}

void ASTPrinter::visitGetExpr(Get& expr) {
    result = "(get " + print(*expr.object) + " " + expr.name.lexeme + ")";
}

void ASTPrinter::visitSetExpr(Set& expr) {
    result = "(set " + print(*expr.object) + " " + expr.name.lexeme + " " + print(*expr.value) + ")";
}

void ASTPrinter::visitThisExpr(This&) {
    result = "this";
}

void ASTPrinter::visitSuperExpr(Super& expr) {
    result = "(super " + expr.method.lexeme + ")";
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

void ASTPrinter::visitFunctionStmt(FunctionStmt& stmt) {
    std::ostringstream out;
    out << "(def " << stmt.name.lexeme << " (";
    for (size_t i = 0; i < stmt.params.size(); i++) {
        if (i > 0) out << " ";
        out << stmt.params[i].lexeme;
    }
    out << ") ";
    for (auto& s : stmt.body) {
        if (!s) continue;
        s->accept(*this);
        out << result << " ";
    }
    out << ")";
    result = out.str();
}

void ASTPrinter::visitReturnStmt(ReturnStmt& stmt) {
    if (stmt.value) {
        result = parenthesize("return", *stmt.value);
    } else {
        result = "(return)";
    }
}

void ASTPrinter::visitClassStmt(ClassStmt& stmt) {
    std::ostringstream out;
    out << "(class " << stmt.name.lexeme << " ";
    for (auto& method : stmt.methods) {
        out << "(method " << method->name.lexeme << ") ";
    }
    out << ")";
    result = out.str();
}
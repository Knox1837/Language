// stmt.h: defines every kind of STATEMENT node in the AST (things that perform an action but don't themselves produce a value like var x = 1;
#pragma once
#include <memory>
#include <optional>
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
struct ImportStmt;
struct BreakStmt;
struct ContinueStmt;
struct ForInStmt;
struct TryStmt;
struct SwitchStmt;

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
    virtual void visitImportStmt(ImportStmt& stmt) = 0;
    virtual void visitBreakStmt(BreakStmt& stmt) = 0;
    virtual void visitContinueStmt(ContinueStmt& stmt) = 0;
    virtual void visitForInStmt(ForInStmt& stmt) = 0;
    virtual void visitTryStmt(TryStmt& stmt) = 0;
    virtual void visitSwitchStmt(SwitchStmt& stmt) = 0;
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

// { statement* }  - introduces a new variable scope
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
// A `for` loop is a WhileStmt with an `increment`: that clause runs after the body on every iteration, INCLUDING an iteration the body ended with `continue`, which is why it is kept apart from the body instead of being appended to it. A plain `while` has no increment.
struct WhileStmt : Stmt {
    ExprPtr condition;
    StmtPtr body;
    ExprPtr increment; // nullptr for a plain while loop
    WhileStmt(ExprPtr condition, StmtPtr body, ExprPtr increment = nullptr)
        : condition(std::move(condition)), body(std::move(body)), increment(std::move(increment)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitWhileStmt(*this); }
};

// def name(param1, param2 = default) { body }
// `defaults` is as long as `params`: defaults[i] is the expression for params[i], or nullptr for a required parameter. Required parameters always come first, so the first `requiredCount()` entries are nullptr and the rest are not.
// With `hasRest`, the LAST entry of `params` is the rest parameter (def f(a, ...rest)): it has no default, takes no argument of its own, and receives an array of every argument beyond the others.
struct FunctionStmt : Stmt {
    Token name;
    std::vector<Token> params;
    std::vector<ExprPtr> defaults;
    bool hasRest;
    std::vector<StmtPtr> body;
    FunctionStmt(Token name, std::vector<Token> params, std::vector<ExprPtr> defaults, bool hasRest, std::vector<StmtPtr> body)
        : name(std::move(name)), params(std::move(params)), defaults(std::move(defaults)), hasRest(hasRest), body(std::move(body)) {}
    // How many parameters take an argument of their own (all of them, except the rest parameter).
    int fixedCount() const { return static_cast<int>(params.size()) - (hasRest ? 1 : 0); }
    int requiredCount() const {
        int required = 0;
        while (required < fixedCount() && !defaults[required]) required++;
        return required;
    }
    void accept(StmtVisitor& visitor) override { visitor.visitFunctionStmt(*this); }
};

// def(param1, param2) { body }   used as an EXPRESSION: an anonymous function that is a value like any other (assign it, pass it, return it).
// It owns a FunctionStmt with an empty name, so the interpreter builds it the same way as a named function and only the binding of a name is skipped. Same lifetime rule as FunctionStmt: the AST must outlive every function created from it.
struct FunctionExpr : Expr {
    std::unique_ptr<FunctionStmt> declaration;
    explicit FunctionExpr(std::unique_ptr<FunctionStmt> declaration) : declaration(std::move(declaration)) {}
    void accept(ExprVisitor& visitor) override { visitor.visitFunctionExpr(*this); }
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
    ExprPtr superclass; // nullptr if no "< Superclass" clause; always a Variable expr, resolved at runtime
    std::vector<std::unique_ptr<FunctionStmt>> methods;
    ClassStmt(Token name, ExprPtr superclass, std::vector<std::unique_ptr<FunctionStmt>> methods)
        : name(std::move(name)), superclass(std::move(superclass)), methods(std::move(methods)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitClassStmt(*this); }
};

// import "path/to/file.mylang" as name;
struct ImportStmt : Stmt {
    Token path;  // the STRING token holding the file path, as written in source
    Token alias; // the IDENTIFIER after "as" - what the module is bound to locally
    std::string sourceDir; // the directory of the file this statement is written in; the path resolves against it. Empty for the main script (the loader's own base directory is used)
    ImportStmt(Token path, Token alias, std::string sourceDir = "")
        : path(std::move(path)), alias(std::move(alias)), sourceDir(std::move(sourceDir)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitImportStmt(*this); }
};

// break;   (only valid inside a loop; the parser rejects it anywhere else)
struct BreakStmt : Stmt {
    Token keyword; // the "break" token, kept for line-number reporting
    explicit BreakStmt(Token keyword) : keyword(std::move(keyword)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitBreakStmt(*this); }
};

// continue;   (only valid inside a loop; the parser rejects it anywhere else)
struct ContinueStmt : Stmt {
    Token keyword;
    explicit ContinueStmt(Token keyword) : keyword(std::move(keyword)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitContinueStmt(*this); }
};

// for (var name in iterable) body
// for (var name, value in iterable) body   -- the two-variable form: for a map `name` is the key and `value` the value, for an array `name` is the index and `value` the element, for a string `name` is the byte's index and `value` the 1-character string. A map's keys and values are both snapshotted before the loop starts.
// The one-variable form runs `body` once per element of an array, once per KEY of a map (keys come out in the map's own sorted order), or once per BYTE of a string (as a 1-character string, like s[i]).
// The iterable is evaluated once, before the loop. Each iteration gets a fresh scope holding its own copy of the loop variable, so a closure created in the body keeps that iteration's value. `break` and `continue` work as in any other loop.
struct ForInStmt : Stmt {
    Token name;      // the loop variable (the key or index in the two-variable form)
    std::optional<Token> valueName; // the second loop variable, if any
    Token paren;     // the ")" closing the clause, kept for line-number reporting (e.g. "Can only iterate over arrays, maps and strings.")
    ExprPtr iterable;
    StmtPtr body;
    ForInStmt(Token name, std::optional<Token> valueName, Token paren, ExprPtr iterable, StmtPtr body)
        : name(std::move(name)), valueName(std::move(valueName)), paren(std::move(paren)), iterable(std::move(iterable)), body(std::move(body)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitForInStmt(*this); }
};

// try { body } catch (name) { handler }
// Runs `body`. If a runtime error is raised inside it (by the language, a built-in, or error(value)), the rest of the body is abandoned and `handler` runs with `name` bound to the error: the thrown value for error(value), the message string for anything else. Both bodies are BlockStmts.
struct TryStmt : Stmt {
    StmtPtr tryBody;
    Token name;        // the catch variable (meaningless when there is no catch)
    std::optional<Token> infoName; // the optional second catch variable: a map {"kind": ..., "line": ..., "trace": [...]} describing the error
    StmtPtr catchBody;   // null when the statement is try/finally with no catch
    StmtPtr finallyBody; // null when there is no finally
    TryStmt(StmtPtr tryBody, Token name, std::optional<Token> infoName, StmtPtr catchBody, StmtPtr finallyBody)
        : tryBody(std::move(tryBody)), name(std::move(name)), infoName(std::move(infoName)), catchBody(std::move(catchBody)), finallyBody(std::move(finallyBody)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitTryStmt(*this); }
};

// One clause of a switch: `case a, b:` followed by statements, or `default:` followed by statements. `values` is empty for the default clause. The body is a list of statements that runs in its own scope.
struct SwitchClause {
    std::vector<ExprPtr> values;
    std::vector<StmtPtr> body;
    bool isDefault = false;
};

// switch (subject) { case a, b: ... case c: ... default: ... }
// The subject is evaluated once, the case values are compared to it with == in order (each one evaluated only when reached), and only the first matching clause runs: there is no fall-through. `default` must be the last clause and runs when nothing matched. `break` inside leaves the switch; `continue` goes on to the enclosing loop.
struct SwitchStmt : Stmt {
    Token keyword; // the "switch" token, for error reporting
    ExprPtr subject;
    std::vector<SwitchClause> clauses;
    SwitchStmt(Token keyword, ExprPtr subject, std::vector<SwitchClause> clauses)
        : keyword(std::move(keyword)), subject(std::move(subject)), clauses(std::move(clauses)) {}
    void accept(StmtVisitor& visitor) override { visitor.visitSwitchStmt(*this); }
};
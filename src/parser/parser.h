// parser.h: declares Parser, which consumes the flat Token stream from the Lexer and builds a tree of Stmt/Expr nodes (the AST). Each grammar
// rule (defined in parser.cpp) gets its own  method, following standard recursive-descent structure
#pragma once
#include <vector>
#include <stdexcept>
#include "../lexer/token.h"
#include "../ast/expr.h"
#include "../ast/stmt.h"

// Thrown internally when a parse rule can't match; caught in synchronize() or at the top level to continue parsing after reporting an error
struct ParseError : std::runtime_error {
    ParseError() : std::runtime_error("parse error") {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Entry point: parses the whole token stream into a list of top-level statements.
    std::vector<StmtPtr> parse();

private:
    std::vector<Token> tokens;
    int current = 0;

    // statement grammar rules
    StmtPtr declaration();      // "class" decl, "def" function, "var" declaration, or falls through to statement()
    StmtPtr classDeclaration(); // "class" IDENTIFIER "{" function* "}"
    std::unique_ptr<FunctionStmt> functionBody(const std::string& kind); // shared by top-level "def" and class methods
    StmtPtr functionDeclaration(); // "def" IDENTIFIER "(" params? ")" block -- wraps functionBody()
    StmtPtr varDeclaration();   // "var" IDENTIFIER ("=" expression)? ";"
    StmtPtr statement();        // dispatches to printStmt/ifStmt/whileStmt/returnStmt/block/exprStmt
    StmtPtr printStatement();   // "print" expression ";"
    StmtPtr ifStatement();      // "if" "(" expression ")" statement ("else" statement)?
    StmtPtr whileStatement();   // "while" "(" expression ")" statement
    StmtPtr forStatement();     // "for" "(" (varDecl|exprStmt|";") expression? ";" expression? ")" statement -- desugars to a whileStmt
    StmtPtr returnStatement();  // "return" expression? ";"
    StmtPtr expressionStatement(); // expression ";"
    std::vector<StmtPtr> block();  // "{" declaration* "}"

    // expression grammar rules, lowest to highest precedence
    ExprPtr expression();  // -> assignment
    ExprPtr assignment();  // -> IDENTIFIER "=" assignment | logicOr
    ExprPtr logicOr();     // -> logicAnd ( "or" logicAnd )*
    ExprPtr logicAnd();    // -> equality ( "and" equality )*
    ExprPtr equality();    // -> comparison ( ("!=" | "==") comparison )*
    ExprPtr comparison();  // -> term ( (">" | ">=" | "<" | "<=") term )*
    ExprPtr term();        // -> factor ( ("-" | "+") factor )*
    ExprPtr factor();      // -> unary ( ("/" | "*") unary )*
    ExprPtr unary();       // -> ("!" | "-") unary | call
    ExprPtr call();        // -> primary ( "(" arguments? ")" | "." IDENTIFIER | "[" expression "]" )*
    ExprPtr finishCall(ExprPtr callee); // parses the argument list once "(" is seen
    ExprPtr primary();     // -> NUMBER | STRING | "true" | "false" | "nil" | "(" expr ")" | IDENTIFIER

    // token-stream helpers
    bool match(std::initializer_list<TokenType> types); // advance+true if current matches any
    bool check(TokenType type) const;   // is current token this type? (no advance)
    Token advance();                    // consume and return current token
    bool isAtEnd() const;
    Token peek() const;
    Token previous() const;
    Token consume(TokenType type, const std::string& message); // expect a token or error
    ParseError error(const Token& token, const std::string& message);
    void synchronize(); // discard tokens until a likely statement boundary, after an error
};
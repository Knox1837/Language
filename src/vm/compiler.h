// compiler.h — a single-pass Pratt parser that reads tokens (reusing the existing Lexer/Token from src/lexer/) and emits bytecode directly into a Chunk, with no separate AST step.
#pragma once
#include <vector>
#include <string>
#include "../lexer/token.h"
#include "chunk.h"

class Compiler {
public:
    // Compiles `source` into `chunk`. Returns false if syntax errors were found (in which case `chunk` is left in an undefined state).
    bool compile(const std::string& source, Chunk& chunk);

private:
    std::vector<Token> tokens;
    size_t current = 0;
    Chunk* chunkOut = nullptr;
    bool hadError = false;

    // Precedence levels, lowest to highest.
    enum class Precedence {
        NONE,
        ASSIGNMENT, // =
        TERM,       // + -
        FACTOR,     // * /
        UNARY,      // -x
        PRIMARY
    };

    // Prefix/infix rules take a `canAssign` flag: true only when the expression being parsed could legally be an assignment target

    using ParseFn = void (Compiler::*)(bool canAssign);
    struct ParseRule {
        ParseFn prefix;
        ParseFn infix;
        Precedence precedence;
    };
    static const ParseRule& getRule(TokenType type);

    void parsePrecedence(Precedence precedence);
    void expression();
    void declaration();
    void varDeclaration();
    void statement();
    void printStatement();
    void expressionStatement();

    // Prefix/infix parse rules: each assumes the relevant token was just consumed (`previous()`), and emits bytecode for it.
    void number(bool canAssign);
    void grouping(bool canAssign);
    void unary(bool canAssign);
    void binary(bool canAssign);
    void variable(bool canAssign);

    // Reads a variable name from `name`, adds it to the constant pool as a string, and returns its constant index
    uint8_t identifierConstant(const Token& name);

    // token-stream helpers (same shape as the tree-walker's Parser)
    const Token& peek() const;
    const Token& previous() const;
    bool isAtEnd() const;
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    void consume(TokenType type, const std::string& message);
    void errorAt(const Token& token, const std::string& message);

    // bytecode emission helpers
    void emitByte(uint8_t byte);
    void emitByte(OpCode op);
    void emitConstant(VMValue value);
    int currentLine() const;
};
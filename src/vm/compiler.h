// compiler.h — a single-pass Pratt parser that reads tokens (reusing the existing Lexer/Token from src/lexer/) and emits bytecode directly into a Chunk, with no separate AST step.
#pragma once
#include <vector>
#include <functional>
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

    // Precedence levels, lowest to highest: mirrors the tree-walker's grammar (equality/comparison/term/factor/unary), collapsed into one table-driven climb instead of one recursive function per level.
    enum class Precedence {
        NONE,
        TERM,       // + -
        FACTOR,     // * /
        UNARY,      // -x
        PRIMARY
    };

    using ParseFn = void (Compiler::*)();
    struct ParseRule {
        ParseFn prefix;
        ParseFn infix;
        Precedence precedence;
    };
    static const ParseRule& getRule(TokenType type);

    void parsePrecedence(Precedence precedence);
    void expression();
    void statement();
    void printStatement();
    void expressionStatement();

    // Prefix/infix parse rules: each assumes the relevant token was just consumed (`previous()`), and emits bytecode for it.
    void number();
    void grouping();
    void unary();
    void binary();

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
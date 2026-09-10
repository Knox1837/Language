// compiler.h:- a single-pass Pratt parser that reads tokens (reusing the existing Lexer/Token from src/lexer/) and emits bytecode directly into a Chunk, with no separate AST step.
#pragma once
#include <vector>
#include <string>
#include "../lexer/token.h"
#include "chunk.h"

// One tracked local variable: its name (for resolving references to it) and the scope depth it was declared at (used to know which locals to discard when a block ends). 
struct LocalVar {
    Token name;
    int depth;
};

class Compiler {
public:
    // Compiles `source` into `chunk`. Returns false (and leaves error messages printed to stderr) if a syntax error was found 
    bool compile(const std::string& source, Chunk& chunk);

private:
    std::vector<Token> tokens;
    size_t current = 0;
    Chunk* chunkOut = nullptr;
    bool hadError = false;

    // Compile-time scope tracking. 
    std::vector<LocalVar> locals;
    int scopeDepth = 0;

    // Precedence levels, lowest to highest. ASSIGNMENT sits below TERM
    // so that e.g. parsing the right-hand side of "x = 1 + 2" correctly consumes the whole "1 + 2" rather than stopping after "1".
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
    void block();       // "{" declaration* "}"
    void beginScope();
    void endScope();

    // Variable-declaration helpers, split out so varDeclaration() can
    // stay agnostic about whether it's declaring a global or a local —
    // the split happens here based on scopeDepth.
    void declareVariable(const Token& name);       // records a LOCAL in `locals` (no-op at global scope)
    void defineVariable(uint8_t globalConstant);    // emits the actual OP_DEFINE_GLOBAL, or nothing for a local
                                                     // (a local's "definition" is just it staying on the stack)
    int resolveLocal(const Token& name);            // returns a local's stack slot, or -1 if not a local

    // Prefix/infix parse rules — each assumes the relevant token was
    // just consumed (`previous()`), and emits bytecode for it.
    void number(bool canAssign);
    void stringLiteral(bool canAssign);
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
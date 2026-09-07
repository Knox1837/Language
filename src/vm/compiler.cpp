// compiler.cpp: implements the Pratt parser to convert tokens to bytecode.
// parsePrecedence() is the heart of it: it looks up the current token's prefix rule (how to start parsing an expression from this token) and repeatedly applies infix rules as long as the next token's precedence is high enough
// This single loop replaces the tree-walker's whole ladder of equality()/comparison()/term()/factor()/unary() functions.

#include "compiler.h"
#include "../lexer/lexer.h"
#include <iostream>
#include <cstdlib>

bool Compiler::compile(const std::string& source, Chunk& chunk) {
    Lexer lexer(source);
    tokens = lexer.scanTokens();
    current = 0;
    chunkOut = &chunk;
    hadError = false;

    while (!isAtEnd()) {
        statement();
    }

    emitByte(OpCode::OP_RETURN);
    return !hadError;
}

// statements

void Compiler::statement() {
    if (match(TokenType::PRINT)) {
        printStatement();
    } else {
        expressionStatement();
    }
}

void Compiler::printStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after value.");
    emitByte(OpCode::OP_PRINT);
}

void Compiler::expressionStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
}

// expressions (Pratt parsing)

void Compiler::expression() {
    parsePrecedence(Precedence::TERM);
}

void Compiler::parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(previous().type).prefix;
    if (!prefixRule) {
        errorAt(previous(), "Expect expression.");
        return;
    }
    (this->*prefixRule)();

    while (precedence <= getRule(peek().type).precedence) {
        advance();
        ParseFn infixRule = getRule(previous().type).infix;
        (this->*infixRule)();
    }
}

void Compiler::number() {
    double value = std::stod(previous().lexeme);
    emitConstant(value);
}

void Compiler::grouping() {
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
}

void Compiler::unary() {
    TokenType opType = previous().type;
    parsePrecedence(Precedence::UNARY); // compile the operand
    if (opType == TokenType::MINUS) {
        emitByte(OpCode::OP_NEGATE);
    }
}

void Compiler::binary() {
    TokenType opType = previous().type;
    const ParseRule& rule = getRule(opType);
    // +1 so same-precedence operators are left-associative: 
    // parsing the right operand stops at, rather than including, another operator of this same precedence (e.g. "1 - 2 - 3" groups as "(1-2)-3").
    parsePrecedence(static_cast<Precedence>(static_cast<int>(rule.precedence) + 1));

    switch (opType) {
        case TokenType::PLUS:  emitByte(OpCode::OP_ADD);      break;
        case TokenType::MINUS: emitByte(OpCode::OP_SUBTRACT); break;
        case TokenType::STAR:  emitByte(OpCode::OP_MULTIPLY); break;
        case TokenType::SLASH: emitByte(OpCode::OP_DIVIDE);   break;
        default: break; // unreachable given the parse-rule table below
    }
}

// parse rule table
// One entry per TokenType this increment cares about; 
// every other token type gets {nullptr, nullptr, NONE} via the default-constructed fallback in getRule().

const Compiler::ParseRule& Compiler::getRule(TokenType type) {
    static const ParseRule numberRule   = { &Compiler::number,   nullptr,          Precedence::NONE };
    static const ParseRule groupingRule = { &Compiler::grouping, nullptr,          Precedence::NONE };
    static const ParseRule unaryRule    = { &Compiler::unary,    nullptr,          Precedence::NONE };
    static const ParseRule termRule     = { nullptr,             &Compiler::binary, Precedence::TERM };
    static const ParseRule factorRule   = { nullptr,             &Compiler::binary, Precedence::FACTOR };
    static const ParseRule minusRule    = { &Compiler::unary,    &Compiler::binary, Precedence::TERM }; // '-' is BOTH unary and binary
    static const ParseRule noRule       = { nullptr,             nullptr,          Precedence::NONE };

    switch (type) {
        case TokenType::NUMBER:      return numberRule;
        case TokenType::LEFT_PAREN:  return groupingRule;
        case TokenType::MINUS:       return minusRule;
        case TokenType::PLUS:        return termRule;
        case TokenType::SLASH:
        case TokenType::STAR:        return factorRule;
        case TokenType::BANG:        return unaryRule; // reserved for later (logical not) — harmless to wire now
        default:                     return noRule;
    }
}

// token-stream helpers

const Token& Compiler::peek() const { return tokens[current]; }
const Token& Compiler::previous() const { return tokens[current - 1]; }
bool Compiler::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }

Token Compiler::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Compiler::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Compiler::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

void Compiler::consume(TokenType type, const std::string& message) {
    if (check(type)) {
        advance();
        return;
    }
    errorAt(peek(), message);
}

void Compiler::errorAt(const Token& token, const std::string& message) {
    hadError = true;
    std::cerr << "[line " << token.line << "] Compile error";
    if (token.type == TokenType::END_OF_FILE) {
        std::cerr << " at end";
    } else {
        std::cerr << " at '" << token.lexeme << "'";
    }
    std::cerr << ": " << message << "\n";
}

// bytecode emission

int Compiler::currentLine() const {
    // previous() is the token most recently consumed: attributing emitted bytecode to it gives reasonable line numbers for errors.
    return current > 0 ? tokens[current - 1].line : 0;
}

void Compiler::emitByte(uint8_t byte) {
    chunkOut->write(byte, currentLine());
}

void Compiler::emitByte(OpCode op) {
    chunkOut->write(op, currentLine());
}

void Compiler::emitConstant(VMValue value) {
    int index = chunkOut->addConstant(value);
    emitByte(OpCode::OP_CONSTANT);
    emitByte(static_cast<uint8_t>(index));
}
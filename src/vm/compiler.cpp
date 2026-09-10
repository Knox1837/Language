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
    locals.clear();
    scopeDepth = 0;

    while (!isAtEnd()) {
        declaration();
    }

    emitByte(OpCode::OP_RETURN);
    return !hadError;
}

// declarations & statements

void Compiler::declaration() {
    if (match(TokenType::VAR)) {
        varDeclaration();
    } else {
        statement();
    }
}

void Compiler::varDeclaration() {
    consume(TokenType::IDENTIFIER, "Expect variable name.");
    Token nameToken = previous();

    declareVariable(nameToken);
    uint8_t globalConstant = (scopeDepth == 0) ? identifierConstant(nameToken) : 0;

    if (match(TokenType::EQUAL)) {
        expression();
    } else {
        // "var x;" with no initializer -> nil, matching the tree-walker's default. 
        emitConstant(VMValue{std::monostate{}});
    }

    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(globalConstant);
}

void Compiler::declareVariable(const Token& name) {
    if (scopeDepth == 0) return; // globals aren't tracked in `locals` at all

    // Disallow redeclaring the same name twice in the SAME block
    // e.g. "{ var a = 1; var a = 2; }"  
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth != -1 && locals[i].depth < scopeDepth) break;
        if (locals[i].name.lexeme == name.lexeme) {
            errorAt(name, "A variable with this name already exists in this scope.");
            return;
        }
    }
    locals.push_back(LocalVar{name, -1});
}

void Compiler::defineVariable(uint8_t globalConstant) {
    if (scopeDepth > 0) {
        // A local doesn't need a runtime "define" instruction at all.
        // its value is already sitting on the stack exactly where it needs to be 
        locals.back().depth = scopeDepth;
        return;
    }
    emitByte(OpCode::OP_DEFINE_GLOBAL);
    emitByte(globalConstant);
}

int Compiler::resolveLocal(const Token& name) {
    // Search backward (innermost/most-recently-declared first) so shadowing resolves to the closest enclosing declaration.
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].name.lexeme == name.lexeme) {
            if (locals[i].depth == -1) {
                errorAt(name, "Cannot read a local variable in its own initializer.");
                return -1;
            }
            return i; // this local's slot IS its index in `locals`,
                      // which mirrors its actual position on the VM stack
        }
    }
    return -1; // not a local — caller falls back to treating it as a global
}

void Compiler::beginScope() {
    scopeDepth++;
}

void Compiler::endScope() {
    scopeDepth--;
    // Pop every local that belonged to the block just exited.
    while (!locals.empty() && locals.back().depth > scopeDepth) {
        emitByte(OpCode::OP_POP);
        locals.pop_back();
    }
}

void Compiler::statement() {
    if (match(TokenType::PRINT)) {
        printStatement();
    } else if (match(TokenType::LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

void Compiler::block() {
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        declaration();
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
}

void Compiler::printStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after value.");
    emitByte(OpCode::OP_PRINT);
}

void Compiler::expressionStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    // Discard the expression's unused result, matching the tree-walker's ExpressionStmt (evaluate and discard) 
    emitByte(OpCode::OP_POP);
}

// expressions (Pratt parsing)

void Compiler::expression() {
    parsePrecedence(Precedence::ASSIGNMENT);
}

void Compiler::parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(previous().type).prefix;
    if (!prefixRule) {
        errorAt(previous(), "Expect expression.");
        return;
    }

    // Only allow the expression we're about to parse to be treated as an assignment TARGET if we were entered at ASSIGNMENT precedence or lower
    bool canAssign = precedence <= Precedence::ASSIGNMENT;
    (this->*prefixRule)(canAssign);

    while (precedence <= getRule(peek().type).precedence) {
        advance();
        ParseFn infixRule = getRule(previous().type).infix;
        (this->*infixRule)(canAssign);
    }

    // If canAssign was true but nothing consumed the "=" (e.g. the parsed expression wasn't a valid assignment target)
    // a stray "=" left over here is a real error rather than silently ignored.
    if (canAssign && match(TokenType::EQUAL)) {
        errorAt(previous(), "Invalid assignment target.");
    }
}

void Compiler::number(bool) {
    double value = std::stod(previous().lexeme);
    emitConstant(value);
}

void Compiler::stringLiteral(bool) {
    // The lexer already strips the surrounding quotes when it produces the STRING token's lexeme, so we can just store it directly as a VMValue string.
    emitConstant(VMValue{previous().lexeme});
}

void Compiler::grouping(bool) {
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
}

void Compiler::unary(bool) {
    TokenType opType = previous().type;
    parsePrecedence(Precedence::UNARY); // compile the operand
    if (opType == TokenType::MINUS) {
        emitByte(OpCode::OP_NEGATE);
    }
}

void Compiler::binary(bool) {
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

void Compiler::variable(bool canAssign) {
    Token name = previous();
    int localSlot = resolveLocal(name);

    OpCode getOp, setOp;
    uint8_t operand;
    if (localSlot != -1) {
        getOp = OpCode::OP_GET_LOCAL;
        setOp = OpCode::OP_SET_LOCAL;
        operand = static_cast<uint8_t>(localSlot);
    } else {
        getOp = OpCode::OP_GET_GLOBAL;
        setOp = OpCode::OP_SET_GLOBAL;
        operand = identifierConstant(name);
    }

    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitByte(setOp);
        emitByte(operand);
    } else {
        emitByte(getOp);
        emitByte(operand);
    }
}

uint8_t Compiler::identifierConstant(const Token& name) {
    // Reuses the same constant pool OP_CONSTANT already draws from. a variable's name is stored as a VMValue string, exactly like a
    // number literal is stored as a VMValue double.
    int index = chunkOut->addConstant(VMValue{name.lexeme});
    return static_cast<uint8_t>(index);
}

// parse rule table
// One entry per TokenType this increment cares about; 
// every other token type gets {nullptr, nullptr, NONE} via the default-constructed fallback in getRule().

const Compiler::ParseRule& Compiler::getRule(TokenType type) {
    static const ParseRule numberRule   = { &Compiler::number,   nullptr,          Precedence::NONE };
    static const ParseRule stringRule   = { &Compiler::stringLiteral, nullptr,     Precedence::NONE };
    static const ParseRule groupingRule = { &Compiler::grouping, nullptr,          Precedence::NONE };
    static const ParseRule unaryRule    = { &Compiler::unary,    nullptr,          Precedence::NONE };
    static const ParseRule termRule     = { nullptr,             &Compiler::binary, Precedence::TERM };
    static const ParseRule factorRule   = { nullptr,             &Compiler::binary, Precedence::FACTOR };
    static const ParseRule minusRule    = { &Compiler::unary,    &Compiler::binary, Precedence::TERM }; // '-' is BOTH unary and binary
    static const ParseRule variableRule = { &Compiler::variable, nullptr,          Precedence::NONE };
    static const ParseRule noRule       = { nullptr,             nullptr,          Precedence::NONE };

    switch (type) {
        case TokenType::NUMBER:      return numberRule;
        case TokenType::STRING:      return stringRule;
        case TokenType::IDENTIFIER:  return variableRule;
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
    // previous() is the token most recently consumed — attributing
    // emitted bytecode to it gives reasonable line numbers for errors.
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
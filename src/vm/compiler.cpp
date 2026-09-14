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

    // For a LOCAL, declareVariable() records it in `locals` right away (before the initializer is compiled) 
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

    // depth is set to -1 ("not yet initialized") rather than scopeDepth immediately
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
    } else if (match(TokenType::IF)) {
        ifStatement();
    } else if (match(TokenType::WHILE)) {
        whileStatement();
    } else if (match(TokenType::FOR)) {
        forStatement();
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

void Compiler::ifStatement() {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    expression(); // condition; leaves its value on the stack
    consume(TokenType::RIGHT_PAREN, "Expect ')' after if condition.");

    // Placeholder jump: if the condition is falsey, skip the then-branch entirely.
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(OpCode::OP_POP); // discard the (truthy) condition value before running the then-branch
    statement();

    // Unconditional jump at the END of the then-branch, to skip past the else-branch entirely
    size_t elseJump = emitJump(OpCode::OP_JUMP);

    patchJump(thenJump); // NOW we know how far to jump if the condition was false, right here
    emitByte(OpCode::OP_POP); // discard the (falsey) condition value before running the else-branch

    if (match(TokenType::ELSE)) {
        statement();
    }
    patchJump(elseJump);
}

void Compiler::whileStatement() {
    size_t loopStart = chunkOut->code.size(); // remember where the condition check begins, to jump back to it

    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after while condition.");

    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(OpCode::OP_POP); // discard the (truthy) condition before running the body
    statement();
    emitLoop(loopStart); // jump BACK to re-check the condition

    patchJump(exitJump);
    emitByte(OpCode::OP_POP); // discard the (falsey) condition that caused the loop to exit
}

void Compiler::forStatement() {
    // "for (init; cond; incr) body" — compiled directly to the equivalent jump/loop bytecode
    beginScope(); // so a "var i" in the init-clause is scoped to the loop, matching the tree-walker

    consume(TokenType::LEFT_PAREN, "Expect '(' after 'for'.");

    if (match(TokenType::SEMICOLON)) {
        // no initializer
    } else if (match(TokenType::VAR)) {
        varDeclaration(); // consumes its own trailing ';'
    } else {
        expressionStatement(); // consumes its own trailing ';'
    }

    size_t loopStart = chunkOut->code.size();

    // Condition is optional; if omitted, treat as "always true" (infinite loop, same as the tree-walker's for-loop desugaring).
    size_t exitJump = static_cast<size_t>(-1);
    bool hasCondition = !check(TokenType::SEMICOLON);
    if (hasCondition) {
        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after loop condition.");
        exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitByte(OpCode::OP_POP); // discard the (truthy) condition
    } else {
        consume(TokenType::SEMICOLON, "Expect ';' after loop condition.");
    }

    // The increment clause is parsed HERE (before the body) but must EXECUTE after the body each iteration. 
    if (!check(TokenType::RIGHT_PAREN)) {
        size_t bodyJump = emitJump(OpCode::OP_JUMP);
        size_t incrementStart = chunkOut->code.size();

        expression(); // the increment expression, e.g. "i = i + 1"
        emitByte(OpCode::OP_POP); // it's a bare expression — discard its unused result

        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);      // after the increment, jump back to re-check the condition
        loopStart = incrementStart; // the BODY's end should now loop back to the increment, not the condition
        patchJump(bodyJump);      // the jump we emitted above lands here — right before the body
    } else {
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");
    }

    statement(); // the loop body
    emitLoop(loopStart);

    if (hasCondition) {
        patchJump(exitJump);
        emitByte(OpCode::OP_POP); // discard the (falsey) condition that caused the loop to exit
    }

    endScope();
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

void Compiler::literal(bool) {
    switch (previous().type) {
        case TokenType::TRUE:  emitByte(OpCode::OP_TRUE);  break;
        case TokenType::FALSE: emitByte(OpCode::OP_FALSE); break;
        case TokenType::NIL:   emitByte(OpCode::OP_NIL);   break;
        default: break; // unreachable given the parse-rule table below
    }
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
    } else if (opType == TokenType::BANG) {
        emitByte(OpCode::OP_NOT);
    }
}

void Compiler::binary(bool) {
    TokenType opType = previous().type;
    const ParseRule& rule = getRule(opType);
    // +1 so same-precedence operators are left-associative: 
    // parsing the right operand stops at, rather than including, another operator of this same precedence (e.g. "1 - 2 - 3" groups as "(1-2)-3").
    parsePrecedence(static_cast<Precedence>(static_cast<int>(rule.precedence) + 1));

    switch (opType) {
        case TokenType::PLUS:          emitByte(OpCode::OP_ADD);      break;
        case TokenType::MINUS:         emitByte(OpCode::OP_SUBTRACT); break;
        case TokenType::STAR:          emitByte(OpCode::OP_MULTIPLY); break;
        case TokenType::SLASH:         emitByte(OpCode::OP_DIVIDE);   break;
        case TokenType::EQUAL_EQUAL:   emitByte(OpCode::OP_EQUAL);    break;
        case TokenType::BANG_EQUAL:    emitByte(OpCode::OP_EQUAL); emitByte(OpCode::OP_NOT); break;
        case TokenType::GREATER:       emitByte(OpCode::OP_GREATER);  break;
        case TokenType::GREATER_EQUAL: emitByte(OpCode::OP_LESS); emitByte(OpCode::OP_NOT); break;
        case TokenType::LESS:          emitByte(OpCode::OP_LESS);     break;
        case TokenType::LESS_EQUAL:    emitByte(OpCode::OP_GREATER); emitByte(OpCode::OP_NOT); break;
        default: break; // unreachable given the parse-rule table below
    }
}

void Compiler::and_(bool) {
    // Short-circuit: if the left operand (already on the stack) is falsey, skip evaluating the right operand entirely and leave the falsey left value as the whole expression's result
    size_t endJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(OpCode::OP_POP); // left was truthy — discard it, result becomes whatever the right side is
    parsePrecedence(Precedence::AND);
    patchJump(endJump);
}

void Compiler::or_(bool) {
    // Mirror of and_(): if the left operand is truthy, skip the right operand and keep the left value as the result.
    // adding a new opcode purely for this.
    size_t elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    size_t endJump = emitJump(OpCode::OP_JUMP);
    patchJump(elseJump);
    emitByte(OpCode::OP_POP);
    parsePrecedence(Precedence::OR);
    patchJump(endJump);
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
    static const ParseRule numberRule     = { &Compiler::number,       nullptr,           Precedence::NONE };
    static const ParseRule stringRule     = { &Compiler::stringLiteral, nullptr,          Precedence::NONE };
    static const ParseRule literalRule    = { &Compiler::literal,      nullptr,           Precedence::NONE };
    static const ParseRule groupingRule   = { &Compiler::grouping,     nullptr,           Precedence::NONE };
    static const ParseRule termRule       = { nullptr,                 &Compiler::binary, Precedence::TERM };
    static const ParseRule factorRule     = { nullptr,                 &Compiler::binary, Precedence::FACTOR };
    static const ParseRule minusRule      = { &Compiler::unary,        &Compiler::binary, Precedence::TERM }; // '-' is BOTH unary and binary
    static const ParseRule bangRule       = { &Compiler::unary,        nullptr,           Precedence::NONE }; // '!' is unary-only
    static const ParseRule equalityRule   = { nullptr,                 &Compiler::binary, Precedence::EQUALITY };
    static const ParseRule comparisonRule = { nullptr,                 &Compiler::binary, Precedence::COMPARISON };
    static const ParseRule andRule        = { nullptr,                 &Compiler::and_,   Precedence::AND };
    static const ParseRule orRule         = { nullptr,                 &Compiler::or_,    Precedence::OR };
    static const ParseRule variableRule   = { &Compiler::variable,     nullptr,           Precedence::NONE };
    static const ParseRule noRule         = { nullptr,                 nullptr,           Precedence::NONE };

    switch (type) {
        case TokenType::NUMBER:         return numberRule;
        case TokenType::STRING:         return stringRule;
        case TokenType::TRUE:
        case TokenType::FALSE:
        case TokenType::NIL:            return literalRule;
        case TokenType::IDENTIFIER:     return variableRule;
        case TokenType::LEFT_PAREN:     return groupingRule;
        case TokenType::MINUS:          return minusRule;
        case TokenType::PLUS:           return termRule;
        case TokenType::SLASH:
        case TokenType::STAR:           return factorRule;
        case TokenType::BANG:           return bangRule;
        case TokenType::BANG_EQUAL:
        case TokenType::EQUAL_EQUAL:    return equalityRule;
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:     return comparisonRule;
        case TokenType::AND:            return andRule;
        case TokenType::OR:             return orRule;
        default:                        return noRule;
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

size_t Compiler::emitJump(OpCode jumpOp) {
    emitByte(jumpOp);
    emitByte(0xFF); // placeholder high byte
    emitByte(0xFF); // placeholder low byte
    return chunkOut->code.size() - 2; // position of the placeholder itself
}

void Compiler::patchJump(size_t jumpPlaceholderOffset) {
    // Distance from right after the 2-byte operand to the current (i.e. "jump to here") position.
    size_t distance = chunkOut->code.size() - jumpPlaceholderOffset - 2;
    if (distance > 0xFFFF) {
        errorAt(previous(), "Too much code to jump over (limit 65535 bytes for this increment).");
        return;
    }
    chunkOut->patchJumpAt(jumpPlaceholderOffset, static_cast<uint16_t>(distance));
}

void Compiler::emitLoop(size_t loopStartOffset) {
    emitByte(OpCode::OP_LOOP); 
    // +2: account for OP_LOOP's own 2-byte operand, which sits between "now" and the jump landing there, otherwise the jump would land 2 bytes short of loopStartOffset.
    size_t distance = chunkOut->code.size() - loopStartOffset + 2;
    if (distance > 0xFFFF) {
        errorAt(previous(), "Loop body too large to jump over (limit 65535 bytes for this increment).");
        return;
    }
    emitByte(static_cast<uint8_t>((distance >> 8) & 0xFF));
    emitByte(static_cast<uint8_t>(distance & 0xFF));
}
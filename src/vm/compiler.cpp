// compiler.cpp: implements the Pratt parser to convert tokens to bytecode.
// parsePrecedence() is the heart of it: it looks up the current token's prefix rule (how to start parsing an expression from this token) and repeatedly applies infix rules as long as the next token's precedence is high enough
// This single loop replaces the tree-walker's whole ladder of equality()/comparison()/term()/factor()/unary() functions.

#include "compiler.h"
#include "../lexer/lexer.h"
#include <iostream>
#include <cstdlib>

std::shared_ptr<VMFunction> Compiler::compile(const std::string& source) {
    Lexer lexer(source);
    tokens = lexer.scanTokens();
    current = 0;
    hadError = lexer.hadError(); // an error the lexer already printed (unknown escape, stray character, unterminated string) means the program is not run
    functionStack.clear();
    classStack.clear();

    // The top-level script is treated as a function too (name "<script>", arity 0) 
    // This is what lets the VM push it onto the same call-frame stack as any real function, uniformly, rather than needing separate "top-level vs. inside a call" execution code.
    auto scriptFunction = std::make_shared<VMFunction>();
    scriptFunction->name = "<script>";
    scriptFunction->arity = 0;
    functionStack.push_back(FunctionState{scriptFunction, {}, {}, 0});

    functionStack.back().locals.push_back(LocalVar{Token(TokenType::IDENTIFIER, "", 0), 0});

    while (!isAtEnd()) {
        declaration();
    }

    emitByte(OpCode::OP_NIL); // implicit "return nil;" if the script/function falls off the end
    emitByte(OpCode::OP_RETURN);

    return hadError ? nullptr : scriptFunction;
}

// declarations & statements

void Compiler::declaration() {
    if (match(TokenType::DEF)) {
        functionDeclaration();
    } else if (match(TokenType::IMPORT)) {
        importDeclaration();
    } else if (match(TokenType::CLASS)) {
        classDeclaration();
    } else if (match(TokenType::VAR)) {
        varDeclaration();
    } else {
        statement();
    }
}

void Compiler::functionDeclaration() {
    consume(TokenType::IDENTIFIER, "Expect function name.");
    Token nameToken = previous();

    // Declared and immediately marked initialized BEFORE compiling the body this is what lets the function reference its own name recursively inside its own body.
    declareVariable(nameToken);
    if (current_().scopeDepth > 0) {
        markInitialized();
    }
    size_t globalConstant = (current_().scopeDepth == 0) ? identifierConstant(nameToken) : 0;

    functionBody(nameToken.lexeme, FunctionType::FUNCTION, "function");

    defineVariable(globalConstant);
}

void Compiler::functionBody(const std::string& name, FunctionType type, const std::string& kind) {
    auto function = std::make_shared<VMFunction>();
    function->name = name;
    functionStack.push_back(FunctionState{function, {}, {}, 0});
    functionStack.back().type = type;
    // Same slot-0 reservation as the top-level script
    bool isMethod = (type == FunctionType::METHOD || type == FunctionType::INITIALIZER);
    functionStack.back().locals.push_back(LocalVar{isMethod ? Token(TokenType::THIS, "this", 0) : Token(TokenType::IDENTIFIER, "", 0), 0});

    beginScope();

    consume(TokenType::LEFT_PAREN, "Expect '(' after " + kind + " name.");
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            function->arity++;
            if (function->arity > 255) {
                errorAt(peek(), "Can't have more than 255 parameters.");
            }
            consume(TokenType::IDENTIFIER, "Expect parameter name.");
            Token paramName = previous();
            declareVariable(paramName);
            markInitialized(); // a parameter's value is already sitting in its stack slot by the time the body runs (the caller pushed it as an argument) 
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

    consume(TokenType::LEFT_BRACE, "Expect '{' before " + kind + " body.");
    block();

    emitReturn(); // implicit return if the body falls off the end without an explicit one: nil, or `this` for an initializer

    // Capture the finished function's upvalue list BEFORE popping its FunctionState (which owns that list)
    function->upvalueCount = static_cast<int>(current_().upvalues.size());
    std::vector<UpvalueInfo> capturedUpvalues = std::move(current_().upvalues);

    // No endScope() call here
    functionStack.pop_back();

    // Unlike a plain value constant (emitConstant()), a function needs OP_CLOSURE — not OP_CONSTANT, followed by one (isLocal, index) byte-pair per upvalue it captures, so the VM knows exactly how to build this specific closure instance at the moment this bytecode runs.
    emitConstantOp(OpCode::OP_CLOSURE, makeConstant(VMValue{function}));
    for (const UpvalueInfo& upvalue : capturedUpvalues) {
        emitByte(upvalue.isLocal ? 1 : 0);
        emitByte(upvalue.index);
    }
}

void Compiler::emitReturn() {
    if (current_().type == FunctionType::INITIALIZER) {
        emitByte(OpCode::OP_GET_LOCAL); // an initializer always returns the instance it was called on, which sits in slot 0
        emitByte(static_cast<uint8_t>(0));
    } else {
        emitByte(OpCode::OP_NIL);
    }
    emitByte(OpCode::OP_RETURN);
}

void Compiler::classDeclaration() {
    consume(TokenType::IDENTIFIER, "Expect class name.");
    Token className = previous();
    size_t nameConstant = identifierConstant(className);
    declareVariable(className);

    emitConstantOp(OpCode::OP_CLASS, nameConstant);
    // Defined BEFORE the body is compiled, so methods can refer to their own class by name (for a global that's a global lookup at call time; for a local it's an upvalue).
    defineVariable(nameConstant);

    classStack.push_back(false);

    if (match(TokenType::LESS)) {
        consume(TokenType::IDENTIFIER, "Expect superclass name.");
        Token superName = previous();
        variable(false); // pushes the superclass value
        if (superName.lexeme == className.lexeme) {
            errorAt(superName, "A class can't inherit from itself.");
        }
        beginScope();
        current_().locals.push_back(LocalVar{Token(TokenType::SUPER, "super", superName.line), -1});
        markInitialized();

        namedVariable(className, false); // [.. superclass subclass]
        emitByte(OpCode::OP_INHERIT);    // checks and links, then pops the subclass: [.. superclass]
        classStack.back() = true;
    }

    namedVariable(className, false); // the class itself, kept on the stack while the methods are attached to it
    consume(TokenType::LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        method();
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after class body.");
    emitByte(OpCode::OP_POP); // the class

    if (classStack.back()) {
        endScope(); // discards (or closes, if a method captured it) the hidden `super` local
    }
    classStack.pop_back();
}

void Compiler::method() {
    consume(TokenType::IDENTIFIER, "Expect method name.");
    Token name = previous();
    size_t nameConstant = identifierConstant(name);

    // Like the tree-walker, a method named "init" is the initializer, whatever else is going on.
    FunctionType type = (name.lexeme == "init") ? FunctionType::INITIALIZER : FunctionType::METHOD;
    functionBody(name.lexeme, type, "method"); // leaves the new closure on the stack

    emitConstantOp(OpCode::OP_METHOD, nameConstant);
}

void Compiler::varDeclaration() {
    consume(TokenType::IDENTIFIER, "Expect variable name.");
    Token nameToken = previous();

    // For a LOCAL, declareVariable() records it in `locals` right away (before the initializer is compiled) 
    declareVariable(nameToken);
    size_t globalConstant = (current_().scopeDepth == 0) ? identifierConstant(nameToken) : 0;

    if (match(TokenType::EQUAL)) {
        expression();
    } else {
        // "var x;" with no initializer -> nil, matching the tree-walker's default. 
        emitConstant(VMValue{std::monostate{}});
    }

    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(globalConstant);
}

void Compiler::importDeclaration() {
    consume(TokenType::STRING, "Expect a string file path after 'import'.");
    // OP_IMPORT is emitted right after the path so a runtime error (missing file, circular import) reports the line of the path, like the tree-walker.
    emitConstantOp(OpCode::OP_IMPORT, makeConstant(VMValue{previous().lexeme}));

    consume(TokenType::AS, "Expect 'as' after import path.");
    consume(TokenType::IDENTIFIER, "Expect a name after 'as'.");
    Token alias = previous();
    consume(TokenType::SEMICOLON, "Expect ';' after import statement.");

    declareVariable(alias); // a no-op at global scope; for a local, the pushed module object already sits in the slot it names
    size_t globalConstant = (current_().scopeDepth == 0) ? identifierConstant(alias) : 0;
    defineVariable(globalConstant);
}

void Compiler::declareVariable(const Token& name) {
    if (current_().scopeDepth == 0) return; // globals aren't tracked in `locals` at all

    auto& locals = current_().locals;

    // Disallow redeclaring the same name twice in the SAME block
    // e.g. "{ var a = 1; var a = 2; }"  
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].depth != -1 && locals[i].depth < current_().scopeDepth) break;
        if (locals[i].name.lexeme == name.lexeme) {
            errorAt(name, "A variable with this name already exists in this scope.");
            return;
        }
    }

    // depth is set to -1 ("not yet initialized") rather than scopeDepth immediately
    locals.push_back(LocalVar{name, -1});
}

void Compiler::markInitialized() {
    if (current_().scopeDepth == 0) return; // no-op for globals — nothing to mark
    current_().locals.back().depth = current_().scopeDepth;
}

void Compiler::defineVariable(size_t globalConstant) {
    if (current_().scopeDepth > 0) {
        markInitialized();
        return;
    }
    emitConstantOp(OpCode::OP_DEFINE_GLOBAL, globalConstant);
}

int Compiler::resolveLocal(FunctionState& state, const Token& name) {
    // Search backward (innermost/most-recently-declared first) so shadowing resolves to the closest enclosing declaration.
    for (int i = static_cast<int>(state.locals.size()) - 1; i >= 0; i--) {
        if (state.locals[i].name.lexeme == name.lexeme) {
            if (state.locals[i].depth == -1) {
                // Found the name, but it's this same declaration, still mid-initializer (see declareVariable()'s sentinel)
                errorAt(name, "Cannot read a local variable in its own initializer.");
                return -1;
            }
            return i; // this local's slot IS its index in `locals`,
                      // relative to this function's OWN call frame
        }
    }
    return -1; // not a local — caller falls back to treating it as a global
}

int Compiler::resolveUpvalue(int functionIndex, const Token& name) {
    if (functionIndex == 0) {
        return -1; // no enclosing function — this IS the top-level script
    }
    int enclosingIndex = functionIndex - 1; // functionStack is nested in compile order, so the immediately enclosing function is always right below this one

    // First check: is `name` a LOCAL declared directly in the immediately enclosing function? If so, this is a one-level capture.
    int local = resolveLocal(functionStack[enclosingIndex], name);
    if (local != -1) {
        // Mark it captured so its scope-exit emits OP_CLOSE_UPVALUE (heap-preserve it) instead of a plain OP_POP 
        functionStack[enclosingIndex].locals[local].isCaptured = true;
        return addUpvalue(functionIndex, static_cast<uint8_t>(local), true);
    }

    // Not a direct local of the enclosing function
    int upvalue = resolveUpvalue(enclosingIndex, name);
    if (upvalue != -1) {
        return addUpvalue(functionIndex, static_cast<uint8_t>(upvalue), false);
    }

    return -1; // not found anywhere outward either — caller treats it as a global
}

int Compiler::addUpvalue(int functionIndex, uint8_t index, bool isLocal) {
    auto& upvalues = functionStack[functionIndex].upvalues;

    // Reuse an existing capture for the exact same (index, isLocal) pair if this function already captures it — avoids redundant duplicate upvalue slots
    for (size_t i = 0; i < upvalues.size(); i++) {
        if (upvalues[i].index == index && upvalues[i].isLocal == isLocal) {
            return static_cast<int>(i);
        }
    }

    if (upvalues.size() >= 256) {
        errorAt(previous(), "Too many closure variables captured in one function (limit 256).");
        return 0;
    }

    upvalues.push_back(UpvalueInfo{index, isLocal});
    return static_cast<int>(upvalues.size() - 1);
}

void Compiler::beginScope() {
    current_().scopeDepth++;
}

void Compiler::endScope() {
    current_().scopeDepth--;
    auto& locals = current_().locals;
    // Pop every local that belonged to the block just exited. 
    // Emitted as individual OP_POP instructions (one per local) rather than a single "pop N" instruction
    while (!locals.empty() && locals.back().depth > current_().scopeDepth) {
        if (locals.back().isCaptured) {
            emitByte(OpCode::OP_CLOSE_UPVALUE);
        } else {
            emitByte(OpCode::OP_POP);
        }
        locals.pop_back();
    }
}

void Compiler::statement() {
    if (match(TokenType::PRINT)) {
        printStatement();
    } else if (match(TokenType::RETURN)) {
        returnStatement();
    } else if (match(TokenType::IF)) {
        ifStatement();
    } else if (match(TokenType::WHILE)) {
        whileStatement();
    } else if (match(TokenType::FOR)) {
        forStatement();
    } else if (match(TokenType::BREAK)) {
        breakStatement();
    } else if (match(TokenType::CONTINUE)) {
        continueStatement();
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

void Compiler::returnStatement() {
    if (current_().type == FunctionType::INITIALIZER) {
        // Like the tree-walker, an initializer's return value is ignored: it always returns `this`. A returned expression is still EVALUATED (for its side effects) and then discarded.
        if (!match(TokenType::SEMICOLON)) {
            expression();
            consume(TokenType::SEMICOLON, "Expect ';' after return value.");
            emitByte(OpCode::OP_POP);
        }
        emitReturn();
        return;
    }

    if (match(TokenType::SEMICOLON)) {
        // Bare "return;" -> nil, matching the tree-walker's ReturnStmt
        // with no value expression.
        emitByte(OpCode::OP_NIL);
    } else {
        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after return value.");
    }
    emitByte(OpCode::OP_RETURN);
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

    patchJump(thenJump); // NOW we know how far to jump if the condition was false — right here
    emitByte(OpCode::OP_POP); // discard the (falsey) condition value before running the else-branch

    if (match(TokenType::ELSE)) {
        statement();
    }
    patchJump(elseJump);
}

void Compiler::whileStatement() {
    size_t loopStart = currentChunk().code.size(); // remember where the condition check begins, to jump back to it

    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after while condition.");

    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitByte(OpCode::OP_POP); // discard the (truthy) condition before running the body

    current_().loops.push_back(LoopInfo{current_().scopeDepth, loopStart, {}});
    statement();
    LoopInfo loop = std::move(current_().loops.back()); // by value: compiling the body may have grown functionStack, so no reference is held across it
    current_().loops.pop_back();

    emitLoop(loopStart); // jump BACK to re-check the condition

    patchJump(exitJump);
    emitByte(OpCode::OP_POP); // discard the (falsey) condition that caused the loop to exit

    // `break` lands here: after the condition has been discarded, because a break happens inside the body, where the condition was already popped.
    for (size_t breakJump : loop.breakJumps) patchJump(breakJump);
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

    size_t loopStart = currentChunk().code.size();

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
        size_t incrementStart = currentChunk().code.size();

        expression(); // the increment expression, e.g. "i = i + 1"
        emitByte(OpCode::OP_POP); // it's a bare expression — discard its unused result

        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);      // after the increment, jump back to re-check the condition
        loopStart = incrementStart; // the BODY's end should now loop back to the increment, not the condition
        patchJump(bodyJump);      // the jump we emitted above lands here — right before the body
    } else {
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");
    }

    // From here `loopStart` is where `continue` must go: the increment clause if there is one (it was just assigned above), otherwise the condition.
    // The loop's scope depth is the for-scope's own (it holds `var i`), so break/continue leave `i` alone and discard only what the body declared.
    current_().loops.push_back(LoopInfo{current_().scopeDepth, loopStart, {}});
    statement(); // the loop body
    LoopInfo loop = std::move(current_().loops.back());
    current_().loops.pop_back();

    emitLoop(loopStart);

    if (hasCondition) {
        patchJump(exitJump);
        emitByte(OpCode::OP_POP); // discard the (falsey) condition that caused the loop to exit
    }

    // `break` lands here: after the loop (and after the condition was discarded), but BEFORE endScope() pops the loop variable, which the break path still has on the stack.
    for (size_t breakJump : loop.breakJumps) patchJump(breakJump);

    endScope();
}

void Compiler::expressionStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    // Discard the expression's unused result, matching the tree-walker's ExpressionStmt (evaluate and discard) 
    emitByte(OpCode::OP_POP);
}

// expressions (Pratt parsing)

// Maps a compound-assignment token (+= -= *= /= %=) to the arithmetic opcode it applies.
// Returns false if `type` isn't one, leaving `op` untouched.
static bool compoundAssignOp(TokenType type, OpCode& op) {
    switch (type) {
        case TokenType::PLUS_EQUAL:    op = OpCode::OP_ADD;      return true;
        case TokenType::MINUS_EQUAL:   op = OpCode::OP_SUBTRACT; return true;
        case TokenType::STAR_EQUAL:    op = OpCode::OP_MULTIPLY; return true;
        case TokenType::SLASH_EQUAL:   op = OpCode::OP_DIVIDE;   return true;
        case TokenType::PERCENT_EQUAL: op = OpCode::OP_MODULO;   return true;
        default: return false;
    }
}

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
    // Same for a leftover compound operator (e.g. "1 += 2" or "a + b += c"); same message as the tree-walker's parser.
    OpCode ignored = OpCode::OP_ADD;
    if (canAssign && compoundAssignOp(peek().type, ignored)) {
        advance();
        errorAt(previous(), "Invalid compound assignment target.");
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
        case TokenType::PERCENT:       emitByte(OpCode::OP_MODULO);   break;
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
    namedVariable(previous(), canAssign);
}

void Compiler::namedVariable(const Token& name, bool canAssign) {
    int localSlot = resolveLocal(current_(), name);

    OpCode getOp, setOp;
    size_t operand;           // a local/upvalue slot (one byte) or, for a global, a constant-pool index (may need OP_WIDE)
    bool isGlobal = false;
    if (localSlot != -1) {
        getOp = OpCode::OP_GET_LOCAL;
        setOp = OpCode::OP_SET_LOCAL;
        operand = static_cast<uint8_t>(localSlot);
    } else {
        int upvalueSlot = resolveUpvalue(static_cast<int>(functionStack.size()) - 1, name);
        if (upvalueSlot != -1) {
            getOp = OpCode::OP_GET_UPVALUE;
            setOp = OpCode::OP_SET_UPVALUE;
            operand = static_cast<uint8_t>(upvalueSlot);
        } else {
            getOp = OpCode::OP_GET_GLOBAL;
            setOp = OpCode::OP_SET_GLOBAL;
            operand = identifierConstant(name);
            isGlobal = true;
        }
    }
    auto emitOp = [&](OpCode op) {
        if (isGlobal) {
            emitConstantOp(op, operand);
        } else {
            emitByte(op);
            emitByte(static_cast<uint8_t>(operand));
        }
    };

    OpCode compoundOp = OpCode::OP_ADD; // only meaningful when compoundAssignOp() returns true below
    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitOp(setOp);
    } else if (canAssign && compoundAssignOp(peek().type, compoundOp)) {
        // "x op= value" means "x = x op value": read x, evaluate value, apply op, store back.
        advance();
        emitOp(getOp);
        expression();
        emitByte(compoundOp);
        emitOp(setOp);
    } else {
        emitOp(getOp);
    }
}

void Compiler::call(bool) {
    uint8_t argCount = argumentList();
    emitByte(OpCode::OP_CALL);
    emitByte(argCount);
}

void Compiler::thisExpr(bool) {
    if (classStack.empty()) {
        // The tree-walker only finds this out at run time ("Undefined variable 'this'."); the VM can tell from the source text alone.
        errorAt(previous(), "Can't use 'this' outside of a class.");
        return;
    }
    namedVariable(previous(), false); // `this` is simply local slot 0 of the enclosing method (or an upvalue of it, from a nested function)
}

void Compiler::superExpr(bool) {
    Token keyword = previous();
    if (classStack.empty()) {
        errorAt(keyword, "Can't use 'super' outside of a class.");
    } else if (!classStack.back()) {
        errorAt(keyword, "Can't use 'super' in a class with no superclass.");
    }
    consume(TokenType::DOT, "Expect '.' after 'super'.");
    consume(TokenType::IDENTIFIER, "Expect superclass method name.");
    size_t nameConstant = identifierConstant(previous());

    namedVariable(Token(TokenType::THIS, "this", keyword.line), false);   // the receiver ...
    namedVariable(Token(TokenType::SUPER, "super", keyword.line), false); // ... and the superclass of the class this code was written in
    emitConstantOp(OpCode::OP_GET_SUPER, nameConstant);
}

uint8_t Compiler::argumentList() {
    uint8_t count = 0;
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            expression();
            if (count == 255) {
                errorAt(previous(), "Can't have more than 255 arguments.");
            }
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return count;
}

void Compiler::arrayLiteral(bool) {
    // The "[" is already consumed. Each element's value is left on the stack; OP_ARRAY then gathers them into one array.
    int count = 0;
    if (!check(TokenType::RIGHT_BRACKET)) {
        do {
            expression();
            if (count == 65535) {
                errorAt(previous(), "Can't have more than 65535 elements in an array literal.");
            }
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACKET, "Expect ']' after array elements.");
    emitByte(OpCode::OP_ARRAY);
    emitByte(static_cast<uint8_t>((count >> 8) & 0xFF)); // 2-byte big-endian count, like jump offsets
    emitByte(static_cast<uint8_t>(count & 0xFF));
}

void Compiler::mapLiteral(bool) {
    // The "{" is already consumed. Keys must be string LITERALS (like the tree-walker); each entry leaves its key, then its value, on the stack, and OP_MAP gathers them all.
    int count = 0;
    if (!check(TokenType::RIGHT_BRACE)) {
        do {
            consume(TokenType::STRING, "Expect string key in map literal.");
            emitConstant(VMValue{previous().lexeme});
            consume(TokenType::COLON, "Expect ':' after map key.");
            expression();
            if (count == 65535) {
                errorAt(previous(), "Can't have more than 65535 entries in a map literal.");
            }
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after map entries.");
    emitByte(OpCode::OP_MAP);
    emitByte(static_cast<uint8_t>((count >> 8) & 0xFF)); // 2-byte big-endian count, like array literals and jumps
    emitByte(static_cast<uint8_t>(count & 0xFF));
}

void Compiler::index(bool canAssign) {
    // The object is already on the stack; the "[" is consumed.
    expression();
    consume(TokenType::RIGHT_BRACKET, "Expect ']' after index.");

    OpCode compoundOp = OpCode::OP_ADD; // only meaningful when compoundAssignOp() returns true below
    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitByte(OpCode::OP_SET_INDEX);
    } else if (canAssign && compoundAssignOp(peek().type, compoundOp)) {
        // "a[i] op= v": unlike a plain variable, naming the target twice would evaluate `a` and `i` twice (a[next()] += 1 would call next() twice). So duplicate the already-evaluated pair instead: read through the copy, leave the original pair for the store.
        advance();
        emitByte(OpCode::OP_DUP2);      // [a i]         -> [a i a i]
        emitByte(OpCode::OP_GET_INDEX); // [a i a i]     -> [a i current]   (type and range errors surface here, BEFORE the right side runs, like the tree-walker)
        expression();                    //               -> [a i current v]
        emitByte(compoundOp);            //               -> [a i result]
        emitByte(OpCode::OP_SET_INDEX); //               -> [result]
    } else {
        emitByte(OpCode::OP_GET_INDEX);
    }
}

void Compiler::dot(bool canAssign) {
    consume(TokenType::IDENTIFIER, "Expect property name after '.'.");
    size_t nameConstant = identifierConstant(previous());

    OpCode compoundOp = OpCode::OP_ADD; // only meaningful when compoundAssignOp() returns true below
    if (canAssign && match(TokenType::EQUAL)) {
        // The tree-walker checks that the object can hold fields BEFORE evaluating the right side, so a bad target must fail before the right side's side effects.
        emitByte(OpCode::OP_CHECK_FIELDS);
        expression();
        emitConstantOp(OpCode::OP_SET_PROPERTY, nameConstant);
    } else if (canAssign && compoundAssignOp(peek().type, compoundOp)) {
        // "obj.x op= v": like a[i] += v, the object must be evaluated ONCE, so duplicate it instead of naming it twice.
        advance();
        emitByte(OpCode::OP_CHECK_FIELDS);
        emitByte(OpCode::OP_DUP);          // [obj]      -> [obj obj]
        emitConstantOp(OpCode::OP_GET_PROPERTY, nameConstant); // [obj obj]  -> [obj current]
        expression();                      //            -> [obj current v]
        emitByte(compoundOp);              //            -> [obj result]
        emitConstantOp(OpCode::OP_SET_PROPERTY, nameConstant); //            -> [result]
    } else {
        emitConstantOp(OpCode::OP_GET_PROPERTY, nameConstant);
    }
}

size_t Compiler::identifierConstant(const Token& name) {
    // Reuses the same constant pool OP_CONSTANT already draws from. a variable's name is stored as a VMValue string, exactly like a number literal is stored as a VMValue double.
    return makeConstant(VMValue{name.lexeme});
}

size_t Compiler::makeConstant(VMValue value) {
    int index = currentChunk().addConstant(std::move(value));
    if (index < 0) {
        if (!current_().reportedConstantOverflow) {
            current_().reportedConstantOverflow = true;
            errorAt(previous(), "Too many constants in one function.");
        }
        return 0;
    }
    return static_cast<size_t>(index);
}

// parse rule table
// One entry per TokenType this increment cares about; every other token type gets {nullptr, nullptr, NONE} via the default-constructed fallback in getRule().

const Compiler::ParseRule& Compiler::getRule(TokenType type) {
    static const ParseRule numberRule     = { &Compiler::number,       nullptr,           Precedence::NONE };
    static const ParseRule stringRule     = { &Compiler::stringLiteral, nullptr,          Precedence::NONE };
    static const ParseRule literalRule    = { &Compiler::literal,      nullptr,           Precedence::NONE };
    static const ParseRule termRule       = { nullptr,                 &Compiler::binary, Precedence::TERM };
    static const ParseRule factorRule     = { nullptr,                 &Compiler::binary, Precedence::FACTOR };
    static const ParseRule minusRule      = { &Compiler::unary,        &Compiler::binary, Precedence::TERM }; // '-' is BOTH unary and binary
    static const ParseRule bangRule       = { &Compiler::unary,        nullptr,           Precedence::NONE }; // '!' is unary-only
    static const ParseRule equalityRule   = { nullptr,                 &Compiler::binary, Precedence::EQUALITY };
    static const ParseRule comparisonRule = { nullptr,                 &Compiler::binary, Precedence::COMPARISON };
    static const ParseRule andRule        = { nullptr,                 &Compiler::and_,   Precedence::AND };
    static const ParseRule orRule         = { nullptr,                 &Compiler::or_,    Precedence::OR };
    static const ParseRule variableRule   = { &Compiler::variable,     nullptr,           Precedence::NONE };
    static const ParseRule parenRule      = { &Compiler::grouping,     &Compiler::call,   Precedence::CALL };
    static const ParseRule bracketRule    = { &Compiler::arrayLiteral, &Compiler::index,  Precedence::CALL }; // '[' starts an array literal in prefix position and an index in infix position
    static const ParseRule dotRule        = { nullptr,                 &Compiler::dot,    Precedence::CALL };
    static const ParseRule braceRule      = { &Compiler::mapLiteral,   nullptr,           Precedence::NONE }; // prefix only: a map literal
    static const ParseRule thisRule       = { &Compiler::thisExpr,     nullptr,           Precedence::NONE };
    static const ParseRule superRule      = { &Compiler::superExpr,    nullptr,           Precedence::NONE };
    static const ParseRule noRule         = { nullptr,                 nullptr,           Precedence::NONE };

    switch (type) {
        case TokenType::NUMBER:         return numberRule;
        case TokenType::STRING:         return stringRule;
        case TokenType::TRUE:
        case TokenType::FALSE:
        case TokenType::NIL:            return literalRule;
        case TokenType::IDENTIFIER:     return variableRule;
        case TokenType::LEFT_PAREN:     return parenRule;
        case TokenType::LEFT_BRACKET:   return bracketRule;
        case TokenType::LEFT_BRACE:     return braceRule;
        case TokenType::THIS:           return thisRule;
        case TokenType::SUPER:          return superRule;
        case TokenType::DOT:            return dotRule;
        case TokenType::MINUS:          return minusRule;
        case TokenType::PLUS:           return termRule;
        case TokenType::SLASH:
        case TokenType::STAR:
        case TokenType::PERCENT:        return factorRule;
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
    currentChunk().write(byte, currentLine());
}

void Compiler::emitByte(OpCode op) {
    currentChunk().write(op, currentLine());
}

void Compiler::emitConstant(VMValue value) {
    emitConstantOp(OpCode::OP_CONSTANT, makeConstant(std::move(value)));
}

void Compiler::emitConstantOp(OpCode op, size_t constantIndex) {
    if (constantIndex <= 0xFF) {
        emitByte(op);
        emitByte(static_cast<uint8_t>(constantIndex));
    } else {
        emitByte(OpCode::OP_WIDE); // tells the VM the operand of the NEXT instruction is 2 bytes
        emitByte(op);
        emitByte(static_cast<uint8_t>((constantIndex >> 8) & 0xFF)); // big-endian, like jump offsets
        emitByte(static_cast<uint8_t>(constantIndex & 0xFF));
    }
}

void Compiler::emitLoopLocalsCleanup(int loopScopeDepth) {
    auto& locals = current_().locals;
    // Innermost first, the same order endScope() uses. The locals stay registered: the code after this jump (the rest of the block) is compiled as usual and its endScope() emits the same pops for the path that does NOT jump.
    for (size_t i = locals.size(); i > 0 && locals[i - 1].depth > loopScopeDepth; --i) {
        emitByte(locals[i - 1].isCaptured ? OpCode::OP_CLOSE_UPVALUE : OpCode::OP_POP);
    }
}

void Compiler::breakStatement() {
    Token keyword = previous();
    consume(TokenType::SEMICOLON, "Expect ';' after 'break'.");
    if (current_().loops.empty()) {
        errorAt(keyword, "Can't use 'break' outside of a loop.");
        return;
    }
    emitLoopLocalsCleanup(current_().loops.back().scopeDepth);
    size_t jump = emitJump(OpCode::OP_JUMP);
    current_().loops.back().breakJumps.push_back(jump);
}

void Compiler::continueStatement() {
    Token keyword = previous();
    consume(TokenType::SEMICOLON, "Expect ';' after 'continue'.");
    if (current_().loops.empty()) {
        errorAt(keyword, "Can't use 'continue' outside of a loop.");
        return;
    }
    emitLoopLocalsCleanup(current_().loops.back().scopeDepth);
    emitLoop(current_().loops.back().continueTarget);
}

size_t Compiler::emitJump(OpCode jumpOp) {
    emitByte(jumpOp);
    emitByte(0xFF); // placeholder high byte
    emitByte(0xFF); // placeholder low byte
    return currentChunk().code.size() - 2; // position of the placeholder itself
}

void Compiler::patchJump(size_t jumpPlaceholderOffset) {
    // Distance from right after the 2-byte operand to the current (i.e. "jump to here") position.
    size_t distance = currentChunk().code.size() - jumpPlaceholderOffset - 2;
    if (distance > 0xFFFF) {
        errorAt(previous(), "Too much code to jump over (limit 65535 bytes for this increment).");
        return;
    }
    currentChunk().patchJumpAt(jumpPlaceholderOffset, static_cast<uint16_t>(distance));
}

void Compiler::emitLoop(size_t loopStartOffset) {
    emitByte(OpCode::OP_LOOP);
    // +2: account for OP_LOOP's own 2-byte operand, which sits between "now" and the jump landing there otherwise the jump would land 2 bytes short of loopStartOffset.
    size_t distance = currentChunk().code.size() - loopStartOffset + 2;
    if (distance > 0xFFFF) {
        errorAt(previous(), "Loop body too large to jump over (limit 65535 bytes for this increment).");
        return;
    }
    emitByte(static_cast<uint8_t>((distance >> 8) & 0xFF));
    emitByte(static_cast<uint8_t>(distance & 0xFF));
}
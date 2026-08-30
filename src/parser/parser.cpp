// parser.cpp: implements the grammar below via recursive descent.
// Each rule is one function; a function calls the functions for the rules
// one level higher in precedence, so precedence is encoded directly in
// the call structure (classic recursive-descent technique).
//
// program     -> declaration* EOF
// declaration -> varDecl | statement
// varDecl     -> "var" IDENTIFIER ( "=" expression )? ";"
// statement   -> exprStmt | printStmt | ifStmt | whileStmt | block
// ifStmt      -> "if" "(" expression ")" statement ( "else" statement )?
// whileStmt   -> "while" "(" expression ")" statement
// block       -> "{" declaration* "}"
// exprStmt    -> expression ";"
// printStmt   -> "print" expression ";"
//
// expression  -> assignment
// assignment  -> IDENTIFIER "=" assignment | logicOr
// logicOr     -> logicAnd ( "or" logicAnd )*
// logicAnd    -> equality ( "and" equality )*
// equality    -> comparison ( ( "!=" | "==" ) comparison )*
// comparison  -> term ( ( ">" | ">=" | "<" | "<=" ) term )*
// term        -> factor ( ( "-" | "+" ) factor )*
// factor      -> unary ( ( "/" | "*" ) unary )*
// unary       -> ( "!" | "-" ) unary | primary
// primary     -> NUMBER | STRING | "true" | "false" | "nil"
//              | "(" expression ")" | IDENTIFIER

#include "parser.h"
#include <iostream>

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

std::vector<StmtPtr> Parser::parse() {
    std::vector<StmtPtr> statements;
    while (!isAtEnd()) {
        statements.push_back(declaration());
    }
    return statements;
}

// statements

StmtPtr Parser::declaration() {
    try {
        if (match({TokenType::VAR})) return varDeclaration();
        return statement();
    } catch (const ParseError&) {
        synchronize();
        return nullptr; // caller should skip nulls; kept simple for this stage
    }
}

StmtPtr Parser::varDeclaration() {
    Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");

    ExprPtr initializer = nullptr;
    if (match({TokenType::EQUAL})) {
        initializer = expression();
    }

    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
    return std::make_unique<VarStmt>(std::move(name), std::move(initializer));
}

StmtPtr Parser::statement() {
    if (match({TokenType::PRINT})) return printStatement();
    if (match({TokenType::IF})) return ifStatement();
    if (match({TokenType::WHILE})) return whileStatement();
    if (match({TokenType::LEFT_BRACE})) {
        return std::make_unique<BlockStmt>(block());
    }
    return expressionStatement();
}

StmtPtr Parser::printStatement() {
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expect ';' after value.");
    return std::make_unique<PrintStmt>(std::move(value));
}

StmtPtr Parser::ifStatement() {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    ExprPtr condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after if condition.");

    StmtPtr thenBranch = statement();
    StmtPtr elseBranch = nullptr;
    if (match({TokenType::ELSE})) {
        elseBranch = statement();
    }

    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
}

StmtPtr Parser::whileStatement() {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
    ExprPtr condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after while condition.");
    StmtPtr body = statement();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

StmtPtr Parser::expressionStatement() {
    ExprPtr expr = expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    return std::make_unique<ExpressionStmt>(std::move(expr));
}

std::vector<StmtPtr> Parser::block() {
    std::vector<StmtPtr> statements;
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        statements.push_back(declaration());
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
    return statements;
}

// ---------- expressions ----------

ExprPtr Parser::expression() { return assignment(); }

ExprPtr Parser::assignment() {
    ExprPtr expr = logicOr();

    if (match({TokenType::EQUAL})) {
        Token equals = previous();
        ExprPtr value = assignment(); // right-associative: a = b = c

        if (auto* varExpr = dynamic_cast<Variable*>(expr.get())) {
            Token name = varExpr->name;
            return std::make_unique<Assign>(std::move(name), std::move(value));
        }

        error(equals, "Invalid assignment target.");
    }

    return expr;
}

ExprPtr Parser::logicOr() {
    ExprPtr expr = logicAnd();
    while (match({TokenType::OR})) {
        Token op = previous();
        ExprPtr right = logicAnd();
        expr = std::make_unique<Logical>(std::move(expr), std::move(op), std::move(right));
    }
    return expr;
}

ExprPtr Parser::logicAnd() {
    ExprPtr expr = equality();
    while (match({TokenType::AND})) {
        Token op = previous();
        ExprPtr right = equality();
        expr = std::make_unique<Logical>(std::move(expr), std::move(op), std::move(right));
    }
    return expr;
}

ExprPtr Parser::equality() {
    ExprPtr expr = comparison();
    while (match({TokenType::BANG_EQUAL, TokenType::EQUAL_EQUAL})) {
        Token op = previous();
        ExprPtr right = comparison();
        expr = std::make_unique<Binary>(std::move(expr), std::move(op), std::move(right));
    }
    return expr;
}

ExprPtr Parser::comparison() {
    ExprPtr expr = term();
    while (match({TokenType::GREATER, TokenType::GREATER_EQUAL, TokenType::LESS, TokenType::LESS_EQUAL})) {
        Token op = previous();
        ExprPtr right = term();
        expr = std::make_unique<Binary>(std::move(expr), std::move(op), std::move(right));
    }
    return expr;
}

ExprPtr Parser::term() {
    ExprPtr expr = factor();
    while (match({TokenType::MINUS, TokenType::PLUS})) {
        Token op = previous();
        ExprPtr right = factor();
        expr = std::make_unique<Binary>(std::move(expr), std::move(op), std::move(right));
    }
    return expr;
}

ExprPtr Parser::factor() {
    ExprPtr expr = unary();
    while (match({TokenType::SLASH, TokenType::STAR})) {
        Token op = previous();
        ExprPtr right = unary();
        expr = std::make_unique<Binary>(std::move(expr), std::move(op), std::move(right));
    }
    return expr;
}

ExprPtr Parser::unary() {
    if (match({TokenType::BANG, TokenType::MINUS})) {
        Token op = previous();
        ExprPtr right = unary();
        return std::make_unique<Unary>(std::move(op), std::move(right));
    }
    return primary();
}

ExprPtr Parser::primary() {
    if (match({TokenType::FALSE})) return std::make_unique<Literal>(LiteralValue{false});
    if (match({TokenType::TRUE})) return std::make_unique<Literal>(LiteralValue{true});
    if (match({TokenType::NIL})) return std::make_unique<Literal>(LiteralValue{std::monostate{}});

    if (match({TokenType::NUMBER})) {
        double value = std::stod(previous().lexeme);
        return std::make_unique<Literal>(LiteralValue{value});
    }

    if (match({TokenType::STRING})) {
        return std::make_unique<Literal>(LiteralValue{previous().lexeme});
    }

    if (match({TokenType::IDENTIFIER})) {
        return std::make_unique<Variable>(previous());
    }

    if (match({TokenType::LEFT_PAREN})) {
        ExprPtr expr = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
        return std::make_unique<Grouping>(std::move(expr));
    }

    throw error(peek(), "Expect expression.");
}

// token-stream helpers

bool Parser::match(std::initializer_list<TokenType> types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

Token Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }

Token Parser::peek() const { return tokens[current]; }

Token Parser::previous() const { return tokens[current - 1]; }

Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw error(peek(), message);
}

ParseError Parser::error(const Token& token, const std::string& message) {
    if (token.type == TokenType::END_OF_FILE) {
        std::cerr << "[line " << token.line << "] Error at end: " << message << "\n";
    } else {
        std::cerr << "[line " << token.line << "] Error at '" << token.lexeme << "': " << message << "\n";
    }
    return ParseError();
}

void Parser::synchronize() {
    advance();
    while (!isAtEnd()) {
        if (previous().type == TokenType::SEMICOLON) return;
        switch (peek().type) {
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::FOR:
            case TokenType::PRINT:
            case TokenType::VAR:
            case TokenType::FUN:
            case TokenType::RETURN:
                return;
            default:
                break;
        }
        advance();
    }
}
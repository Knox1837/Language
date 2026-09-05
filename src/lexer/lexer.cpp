#include "lexer.h"
#include <iostream>

const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"and", TokenType::AND}, {"or", TokenType::OR},
    {"if", TokenType::IF}, {"else", TokenType::ELSE},
    {"true", TokenType::TRUE}, {"false", TokenType::FALSE},
    {"nil", TokenType::NIL}, {"for", TokenType::FOR},
    {"while", TokenType::WHILE}, {"def", TokenType::DEF},
    {"return", TokenType::RETURN}, {"var", TokenType::VAR},
    {"print", TokenType::PRINT}, {"class", TokenType::CLASS}, {"this", TokenType::THIS},
    {"super", TokenType::SUPER},
};
Lexer::Lexer(std::string source) : source(std::move(source)) {}

std::vector<Token> Lexer::scanTokens() {
    // Skip UTF-8 byte-order mark if present (common on Windows-saved files)
    if (source.size() >= 3 &&
        (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
        start = current = 3;
    }

    while (!isAtEnd()) {
        start = current;
        scanToken();
    }
    tokens.emplace_back(TokenType::END_OF_FILE, "", line);
    return tokens;
}

bool Lexer::isAtEnd() const { return current >= (int)source.size(); }

char Lexer::advance() { return source[current++]; }

bool Lexer::match(char expected) {
    if (isAtEnd() || source[current] != expected) return false;
    current++;
    return true;
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source[current];
}

char Lexer::peekNext() const {
    if (current + 1 >= (int)source.size()) return '\0';
    return source[current + 1];
}

void Lexer::addToken(TokenType type) {
    addToken(type, source.substr(start, current - start));
}

void Lexer::addToken(TokenType type, const std::string& literalText) {
    tokens.emplace_back(type, literalText, line);
}

void Lexer::scanToken() {
    char c = advance();
    switch (c) {
        case '(': addToken(TokenType::LEFT_PAREN); break;
        case ')': addToken(TokenType::RIGHT_PAREN); break;
        case '{': addToken(TokenType::LEFT_BRACE); break;
        case '}': addToken(TokenType::RIGHT_BRACE); break;
        case '[': addToken(TokenType::LEFT_BRACKET); break;
        case ']': addToken(TokenType::RIGHT_BRACKET); break;
        case ',': addToken(TokenType::COMMA); break;
        case '.': addToken(TokenType::DOT); break;
        case '-': addToken(TokenType::MINUS); break;
        case '+': addToken(TokenType::PLUS); break;
        case ';': addToken(TokenType::SEMICOLON); break;
        case ':': addToken(TokenType::COLON); break;
        case '*': addToken(TokenType::STAR); break;

        case '!': addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG); break;
        case '=': addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL); break;
        case '<': addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS); break;
        case '>': addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER); break;

        case '/':
            if (match('/')) {
                // comment: consume until end of line
                while (peek() != '\n' && !isAtEnd()) advance();
            } else {
                addToken(TokenType::SLASH);
            }
            break;

        case ' ':
        case '\r':
        case '\t':
            break; // ignore whitespace
        case '\n':
            line++;
            break;

        case '"': string_(); break;

        default:
            if (isDigit(c)) {
                number();
            } else if (isAlpha(c)) {
                identifier();
            } else {
                std::cerr << "[line " << line << "] Unexpected character: " << c << "\n";
            }
            break;
    }
}

void Lexer::string_() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') line++;
        advance();
    }
    if (isAtEnd()) {
        std::cerr << "[line " << line << "] Unterminated string.\n";
        return;
    }
    advance(); // closing quote
    std::string value = source.substr(start + 1, current - start - 2);
    addToken(TokenType::STRING, value);
}

void Lexer::number() {
    while (isDigit(peek())) advance();
    if (peek() == '.' && isDigit(peekNext())) {
        advance();
        while (isDigit(peek())) advance();
    }
    addToken(TokenType::NUMBER, source.substr(start, current - start));
}

void Lexer::identifier() {
    while (isAlphaNumeric(peek())) advance();
    std::string text = source.substr(start, current - start);
    auto it = keywords.find(text);
    TokenType type = (it != keywords.end()) ? it->second : TokenType::IDENTIFIER;
    addToken(type, text);
}

bool Lexer::isDigit(char c) { return c >= '0' && c <= '9'; }
bool Lexer::isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool Lexer::isAlphaNumeric(char c) { return isAlpha(c) || isDigit(c); }
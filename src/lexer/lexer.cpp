// lexer.cpp: Implementation of the Lexer class
#include "lexer.h"
#include <iostream>

const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"and", TokenType::AND}, {"or", TokenType::OR},
    {"if", TokenType::IF}, {"else", TokenType::ELSE},
    {"true", TokenType::TRUE}, {"false", TokenType::FALSE},
    {"nil", TokenType::NIL}, {"for", TokenType::FOR},
    {"while", TokenType::WHILE}, {"def", TokenType::DEF},
    {"break", TokenType::BREAK}, {"continue", TokenType::CONTINUE},
    {"return", TokenType::RETURN}, {"var", TokenType::VAR},
    {"print", TokenType::PRINT}, {"class", TokenType::CLASS}, {"this", TokenType::THIS},
    {"super", TokenType::SUPER},
    {"import", TokenType::IMPORT}, {"as", TokenType::AS}, {"in", TokenType::IN},
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
        case ':': addToken(TokenType::COLON); break;
        case '.': addToken(TokenType::DOT); break;
        case '-': addToken(match('=') ? TokenType::MINUS_EQUAL : TokenType::MINUS); break;
        case '+': addToken(match('=') ? TokenType::PLUS_EQUAL : TokenType::PLUS); break;
        case ';': addToken(TokenType::SEMICOLON); break;
        case '*': addToken(match('=') ? TokenType::STAR_EQUAL : TokenType::STAR); break;
        case '%': addToken(match('=') ? TokenType::PERCENT_EQUAL : TokenType::PERCENT); break;

        case '!': addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG); break;
        case '=': addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL); break;
        case '<': addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS); break;
        case '>': addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER); break;

        case '/':
            if (match('/')) {
                // comment: consume until end of line
                while (peek() != '\n' && !isAtEnd()) advance();
            } else if (match('=')) {
                addToken(TokenType::SLASH_EQUAL);
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
                errored = true;
            }
            break;
    }
}

// Scans a string literal (the opening quote is already consumed) and builds its VALUE: the text between the quotes with escape sequences replaced by the characters they stand for. \n newline    \t tab    \r carriage return    \\ backslash    \" double quote
// A backslash followed by anything else is an error ("Unknown escape sequence"). The two characters are kept as written so the rest of the file still scans, and errored is set so the VM refuses to run.
void Lexer::string_() {
    std::string value;
    while (peek() != '"' && !isAtEnd()) {
        char c = advance();
        if (c == '\n') line++; // a string may span lines
        if (c != '\\') {
            value += c;
            continue;
        }
        if (isAtEnd()) break; // a lone backslash at the very end: reported below as an unterminated string
        char escaped = advance(); // consumed here, so an escaped quote (\") does not end the string
        switch (escaped) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case '\\': value += '\\'; break;
            case '"': value += '"'; break;
            default:
                std::cerr << "[line " << line << "] Unknown escape sequence '\\" << escaped << "' in string.\n";
                errored = true;
                if (escaped == '\n') line++;
                value += '\\';
                value += escaped;
                break;
        }
    }
    if (isAtEnd()) {
        std::cerr << "[line " << line << "] Unterminated string.\n";
        errored = true;
        return;
    }
    advance(); // closing quote
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
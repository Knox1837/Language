// Scans raw source text into a flat list of tokens (unchanged, now just relocated)
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "token.h"

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> scanTokens();

private:
    std::string source;
    std::vector<Token> tokens;
    int start = 0;
    int current = 0;
    int line = 1;

    static const std::unordered_map<std::string, TokenType> keywords;

    bool isAtEnd() const;
    char advance();
    bool match(char expected);
    char peek() const;
    char peekNext() const;

    void scanToken();
    void addToken(TokenType type);
    void addToken(TokenType type, const std::string& literalText);

    void string_();
    void number();
    void identifier();

    static bool isDigit(char c);
    static bool isAlpha(char c);
    static bool isAlphaNumeric(char c);
};
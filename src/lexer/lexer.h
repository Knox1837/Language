// Scans raw source text into a flat list of tokens
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "token.h"

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> scanTokens();

    // True if scanTokens() reported any error (an unexpected character, an unterminated string, an unknown escape sequence). The message has already been printed. The tree-walking interpreter carries on regardless, as it does for any syntax error; the VM refuses to run the program.
    bool hadError() const { return errored; }

private:
    std::string source;
    std::vector<Token> tokens;
    int start = 0;
    int current = 0;
    int line = 1;
    bool errored = false;

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
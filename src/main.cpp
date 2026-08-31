// main.cpp: the entry point to wire the pipeline together: 
//source text -> Lexer -> tokens -> Parser -> AST -> Interpreter -> actual program output

#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

static Interpreter interpreter; // persists across REPL lines so variables/functions survive between them
static std::vector<std::vector<StmtPtr>> allStatements; // keeps every parsed AST alive for the program's lifetime

static void run(const std::string& source) {
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.scanTokens();

    Parser parser(tokens);
    allStatements.push_back(parser.parse());

    interpreter.interpret(allStatements.back());
}

static void runFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Could not open file: " << path << "\n";
        std::exit(74);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    run(buffer.str());
}

static void runPrompt() {
    std::string line;
    std::cout << "mylang> ";
    while (std::getline(std::cin, line)) {
        run(line);
        std::cout << "mylang> ";
    }
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: mylang [script]\n";
        return 64;
    } else if (argc == 2) {
        runFile(argv[1]);
    } else {
        runPrompt();
    }
    return 0;
}
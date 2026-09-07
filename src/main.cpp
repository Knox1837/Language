// main.cpp — the entry point. Wires the pipeline together: source text ->
// Lexer -> tokens -> Parser -> AST -> Interpreter -> actual program output.
//
// IMPORTANT: allStatements accumulates every parsed statement list rather
// than letting each one go out of scope after run(). This is required
// because UserFunction keeps a raw pointer back into the AST (see the
// comment on FunctionStmt in stmt.h) — if a REPL line's AST were freed
// after that line ran, a function defined on that line would dangle.

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
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

    // Relative `import "..."` paths inside this script resolve against the script's own directory, not wherever mylang.exe happened to be launched from 
    // for predictable behabiour equivalent to pythonic imports
    std::filesystem::path scriptDir = std::filesystem::path(path).parent_path();
    if (scriptDir.empty()) scriptDir = ".";
    interpreter.setModuleBaseDir(scriptDir.string());

    run(buffer.str());
}

static void runPrompt() {
    // The REPL has no single script file, so imports resolve relative to
    // the current working directory instead.
    interpreter.setModuleBaseDir(".");

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
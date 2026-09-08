// main.cpp: the entry point. Wires the pipeline together: source text ->
// Lexer -> tokens -> Parser -> AST -> Interpreter -> actual program output.

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"
#include "vm/vm.h"

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
    // The REPL has no single script file, so imports resolve relative to the current working directory instead.
    interpreter.setModuleBaseDir(".");

    std::string line;
    std::cout << "mylang> ";
    while (std::getline(std::cin, line)) {
        run(line);
        std::cout << "mylang> ";
    }
}

static int runVmFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Could not open file: " << path << "\n";
        return 74;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    VM vm;
    InterpretResult result = vm.interpret(buffer.str());
    if (result == InterpretResult::COMPILE_ERROR) return 65;
    if (result == InterpretResult::RUNTIME_ERROR) return 70;
    return 0;
}

static void runVmPrompt() {
    VM vm;
    std::string line;
    std::cout << "mylang-vm> ";
    while (std::getline(std::cin, line)) {
        vm.interpret(line);
        std::cout << "mylang-vm> ";
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);

    bool useVm = false;
    std::vector<std::string> positional;
    for (auto& arg : args) {
        if (arg == "--vm") {
            useVm = true;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() > 1) {
        std::cerr << "Usage: mylang [--vm] [script]\n";
        return 64;
    }

    if (useVm) {
        if (positional.size() == 1) return runVmFile(positional[0]);
        runVmPrompt();
        return 0;
    }

    if (positional.size() == 1) {
        runFile(positional[0]);
    } else {
        runPrompt();
    }
    return 0;
}
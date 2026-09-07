// module_loader.cpp — reads, parses, and runs an imported file in a completely fresh Interpreter (its own global scope + its own stdlib registration)

#include "module_loader.h"
#include "environment.h" // for RuntimeError
#include "module_object.h"
#include "interpreter.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include <fstream>
#include <sstream>
#include <filesystem>

ModuleLoader::ModuleLoader(std::string baseDir) : baseDir(std::move(baseDir)) {}

std::string ModuleLoader::resolvePath(const std::string& path) const {
    namespace fs = std::filesystem;
    fs::path p(path);
    fs::path resolved = p.is_absolute() ? p : fs::path(baseDir) / p;

    // weakly_canonical normalizes ".."/"." segments without requiring the
    // file to already exist (plain canonical() would throw if it doesn't) —
    // this keeps cache keys consistent even before we've confirmed the
    // file is actually readable.
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(resolved, ec);
    return ec ? resolved.string() : canonical.string();
}

Value ModuleLoader::load(const std::string& path, const Token& importToken, Interpreter& /*hostInterpreter*/) {
    std::string resolved = resolvePath(path);

    auto cached = cache.find(resolved);
    if (cached != cache.end()) {
        return cached->second; // already imported elsewhere — reuse the same module object
    }

    if (currentlyLoading.count(resolved)) {
        throw RuntimeError(importToken, "Circular import detected for '" + path + "'.");
    }

    std::ifstream file(resolved);
    if (!file) {
        throw RuntimeError(importToken, "Could not open imported file '" + path + "'.");
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    currentlyLoading.insert(resolved);

    Lexer lexer(buffer.str());
    std::vector<Token> tokens = lexer.scanTokens();

    Parser parser(tokens);
    moduleStatements.push_back(parser.parse());
    // NOTE: moduleStatements.back() is a vector of StmtPtr, which are smart pointers to the AST nodes. 
    // We keep this vector alive for the lifetime of the program to ensure that any functions or closures defined in the imported module can still reference their AST nodes.
    Interpreter moduleInterpreter;
    moduleInterpreter.setSharedModuleLoader(shared_from_this());
    moduleInterpreter.interpret(moduleStatements.back());

    Value moduleValue = std::make_shared<ModuleObject>(moduleInterpreter.getGlobalEnvironment());

    currentlyLoading.erase(resolved);
    cache[resolved] = moduleValue;
    return moduleValue;
}
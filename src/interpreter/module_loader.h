#pragma once
// module_loader.h: resolves, caches, and runs imported files.

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "value.h"
#include "../lexer/token.h"
#include "../ast/stmt.h"

class Interpreter;

class ModuleLoader : public std::enable_shared_from_this<ModuleLoader> {
public:
    // baseDir: the directory the main script lives in 
    // relative import paths are resolved against this, not against wherever the process happened to be launched from.
    explicit ModuleLoader(std::string baseDir);

    // Resolves `path` (relative to baseDir), and either returns the cached ModuleObject for it or runs the file fresh and caches the result. 
    Value load(const std::string& path, const Token& importToken, Interpreter& hostInterpreter);

private:
    std::string baseDir;
    std::unordered_map<std::string, Value> cache;       // resolved path -> ModuleObject
    std::unordered_set<std::string> currentlyLoading;    // for circular-import detection

    // Keeps every imported file's parsed AST alive for the program's  whole lifetime
    std::vector<std::vector<StmtPtr>> moduleStatements;

    std::string resolvePath(const std::string& path) const;
};
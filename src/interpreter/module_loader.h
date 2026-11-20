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
    // baseDir: the directory the main script lives in. A relative import path written in the MAIN script is resolved against this, not against wherever the process happened to be launched from. (One written in an imported file is resolved against that file's own directory.)
    explicit ModuleLoader(std::string baseDir);

    // Resolves `path` and either returns the cached ModuleObject for it or runs the file fresh and caches the result. `fromDir` is the directory of the file the import is written in; empty means the main script, so baseDir is used.
    Value load(const std::string& path, const Token& importToken, Interpreter& hostInterpreter, const std::string& fromDir);

private:
    std::string baseDir;
    std::unordered_map<std::string, Value> cache;       // resolved path -> ModuleObject
    std::unordered_set<std::string> currentlyLoading;    // for circular-import detection

    // Keeps every imported file's parsed AST alive for the program's  whole lifetime
    std::vector<std::vector<StmtPtr>> moduleStatements;

    std::string resolvePath(const std::string& path, const std::string& fromDir) const;
};
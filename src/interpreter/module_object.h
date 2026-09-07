#pragma once
// module_object.h: the runtime representation of an imported file's namespace. 
// Wraps the Environment that resulted from running the imported file's top-level code, and exposes property access (import "x.mylang" as m; m.someFunction())

#include <memory>
#include "value.h"
#include "environment.h"
#include "../lexer/token.h"

class ModuleObject {
public:
    explicit ModuleObject(std::shared_ptr<Environment> moduleEnvironment);

    // m.name: looks up a top-level binding from the imported file.
    // Throws RuntimeError if the name wasn't defined at that file's top level.
    Value get(const Token& name);

private:
    std::shared_ptr<Environment> moduleEnvironment;
};
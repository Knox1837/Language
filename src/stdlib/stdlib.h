// stdlib.h — single entry point called once from Interpreter's constructor to populate the global environment with every built-in function. 
// Add a new lib file's registerXLib() call here to wire it in.
#pragma once
#include <memory>
#include "../interpreter/environment.h"

void registerStdlib(std::shared_ptr<Environment> globals);
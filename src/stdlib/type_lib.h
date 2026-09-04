#pragma once
// type_lib.h — runtime type-checking predicates: isNumber, isString, etc.
#include <memory>
#include "../interpreter/environment.h"

void registerTypeLib(std::shared_ptr<Environment> globals);
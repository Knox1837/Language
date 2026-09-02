// math_lib.h: numeric built-ins beyond the core +-*/ operators.
#pragma once
#include <memory>
#include "../interpreter/environment.h"

void registerMathLib(std::shared_ptr<Environment> globals);
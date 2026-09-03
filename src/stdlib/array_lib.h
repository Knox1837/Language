// array_lib.h: array manipulation built-ins: push, pop, length, contains.
#pragma once

#include <memory>
#include "../interpreter/environment.h"

void registerArrayLib(std::shared_ptr<Environment> globals);
#pragma once
// io_lib.h — input(): reads one line from stdin as a string. 
// To show a prompt, print it yourself first (this language has no default/optional parameters, so a single fixed-arity input() is simplest).
#include <memory>
#include "../interpreter/environment.h"

void registerIoLib(std::shared_ptr<Environment> globals);
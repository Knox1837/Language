#pragma once
// io_lib.h - input(): reads one line from stdin as a string. 
// To show a prompt, print it yourself first (a single fixed-arity input() is simplest, even though user functions can have default parameters).
#include <memory>
#include "../interpreter/environment.h"

void registerIoLib(std::shared_ptr<Environment> globals);
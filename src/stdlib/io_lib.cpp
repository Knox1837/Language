// io_lib.cpp: input() reads a full line from stdin. 
// Returns "" (not an error) on EOF/stream failure, so scripts reading input in a loop can just check for an empty string rather than handling an exception.

#include "io_lib.h"
#include "native_function.h"
#include <iostream>

void registerIoLib(std::shared_ptr<Environment> globals) {
    globals->define("input", Value{std::make_shared<NativeFunction>(
        "input", 0,
        [](Interpreter&, std::vector<Value>&) -> Value {
            std::string line;
            if (!std::getline(std::cin, line)) {
                return std::string(""); // EOF or read failure
            }
            return line;
        }
    )});
}
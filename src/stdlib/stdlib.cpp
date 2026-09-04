// stdlib.cpp: single entry point called once from Interpreter's constructor to populate the global environment with every built-in function.
#include "stdlib.h"
#include "math_lib.h"
#include "string_lib.h"
#include "io_lib.h"
#include "array_lib.h"
#include "type_lib.h"

void registerStdlib(std::shared_ptr<Environment> globals) {
    registerMathLib(globals);
    registerStringLib(globals);
    registerIoLib(globals);
    registerArrayLib(globals);
    registerTypeLib(globals);
}
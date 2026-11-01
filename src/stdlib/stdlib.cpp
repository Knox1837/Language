// stdlib.cpp: single entry point called once from Interpreter's constructor to populate the global environment with every built-in function.
#include "stdlib.h"
#include "math_lib.h"
#include "string_lib.h"
#include "io_lib.h"
#include "array_lib.h"
#include "type_lib.h"
#include "map_lib.h"
#include "native_function.h"

void registerStdlib(std::shared_ptr<Environment> globals) {
    registerMathLib(globals);
    registerStringLib(globals);
    registerIoLib(globals);
    registerArrayLib(globals);
    registerTypeLib(globals);
    registerMapLib(globals);

    // error(value): raises `value` as a runtime error. A `try`/`catch` receives the value itself; uncaught, it prints "[line N] Runtime error: " and the value's printed form.
    globals->define("error", Value{std::make_shared<NativeFunction>("error", 1, [](Interpreter&, std::vector<Value>& args) -> Value {
            throw makeThrownError(Token(TokenType::IDENTIFIER, "error", 0), args[0]);
        }
    )});
}
// stdlib.cpp: single entry point called once from Interpreter's constructor to populate the global environment with every built-in function.
#include "stdlib.h"
#include "math_lib.h"
#include "string_lib.h"
#include "io_lib.h"
#include "array_lib.h"
#include "type_lib.h"
#include "map_lib.h"
#include "native_function.h"
#include "../interpreter/array_object.h"
#include "../util/process.h"

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

    // assert(cond) / assert(cond, message): does nothing when `cond` is truthy; otherwise raises `message` (any value, delivered to `catch` unchanged) or "Assertion failed." if none was given. Raised like error(), so an uncaught one reports the line of the call.
    globals->define("assert", Value{std::make_shared<NativeFunction>("assert", 1, [](Interpreter&, std::vector<Value>& args) -> Value {
            if (isTruthy(args[0])) return std::monostate{};
            Value reason = args.size() > 1 ? args[1] : Value{std::string("Assertion failed.")};
            throw makeThrownError(Token(TokenType::IDENTIFIER, "assert", 0), reason);
        }, 2
    )});

    // exit() / exit(code): ends the program at once with that exit code (default 0). Nothing after it runs, and `catch` can't intercept it.
    globals->define("exit", Value{std::make_shared<NativeFunction>("exit", 0, [](Interpreter&, std::vector<Value>& args) -> Value {
            int code = 0;
            if (!args.empty()) {
                if (!std::holds_alternative<double>(args[0])) throw nativeError("exit", "Expected a number argument.");
                if (!toExitCode(std::get<double>(args[0]), code)) throw nativeError("exit", "Exit code is out of range.");
            }
            exitProcess(code);
        }, 1
    )});

    // args(): the command-line arguments after the script's path, as an array of strings (empty if there were none).
    globals->define("args", Value{std::make_shared<NativeFunction>("args", 0, [](Interpreter&, std::vector<Value>&) -> Value {
            auto result = std::make_shared<ArrayObject>();
            for (const std::string& a : scriptArgs()) result->elements.push_back(a);
            return result;
        }
    )});
}
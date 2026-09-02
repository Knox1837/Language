// math_lib.cpp: clock(), abs(), sqrt(), pow(), floor(), ceil(), round()
// All take/return the language's only numeric type (double)
#include "math_lib.h"
#include "native_function.h"
#include <cmath>
#include <ctime>

static double requireNumber(const std::string& fnName, const Value& v) {
    if (!std::holds_alternative<double>(v)) {
        throw nativeError(fnName, "Expected a number argument.");
    }
    return std::get<double>(v);
}

void registerMathLib(std::shared_ptr<Environment> globals) {
    // clock() — CPU time in seconds since program start. Classic first native function (also in Crafting Interpreters): handy for benchmarking scripts run by this very interpreter.
    globals->define("clock", Value{std::make_shared<NativeFunction>(
        "clock", 0,
        [](Interpreter&, std::vector<Value>&) -> Value {
            return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
        }
    )});

    globals->define("abs", Value{std::make_shared<NativeFunction>(
        "abs", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::fabs(requireNumber("abs", args[0]));
        }
    )});

    globals->define("sqrt", Value{std::make_shared<NativeFunction>(
        "sqrt", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            double n = requireNumber("sqrt", args[0]);
            if (n < 0) throw nativeError("sqrt", "Cannot take the square root of a negative number.");
            return std::sqrt(n);
        }
    )});

    globals->define("pow", Value{std::make_shared<NativeFunction>(
        "pow", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::pow(requireNumber("pow", args[0]), requireNumber("pow", args[1]));
        }
    )});

    globals->define("floor", Value{std::make_shared<NativeFunction>(
        "floor", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::floor(requireNumber("floor", args[0]));
        }
    )});

    globals->define("ceil", Value{std::make_shared<NativeFunction>(
        "ceil", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::ceil(requireNumber("ceil", args[0]));
        }
    )});

    globals->define("round", Value{std::make_shared<NativeFunction>(
        "round", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::round(requireNumber("round", args[0]));
        }
    )});
}
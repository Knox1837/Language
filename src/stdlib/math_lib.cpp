// math_lib.cpp: clock(), abs(), sqrt(), pow(), floor(), ceil(), round(), min(), max(), trig (sin/cos/tan), log/log10, and a seedable random number generator (random/randomInt/setSeed). 
// All numeric types are the language's only number type (double).

#include "math_lib.h"
#include "native_function.h"
#include <cmath>
#include <ctime>
#include <random>

static double requireNumber(const std::string& fnName, const Value& v) {
    if (!std::holds_alternative<double>(v)) {
        throw nativeError(fnName, "Expected a number argument.");
    }
    return std::get<double>(v);
}

// One RNG shared by random()/randomInt() for the whole program run.
static std::mt19937& rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

void registerMathLib(std::shared_ptr<Environment> globals) {
    // clock(): CPU time in seconds since program start
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

    globals->define("min", Value{std::make_shared<NativeFunction>(
        "min", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::min(requireNumber("min", args[0]), requireNumber("min", args[1]));
        }
    )});

    globals->define("max", Value{std::make_shared<NativeFunction>(
        "max", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::max(requireNumber("max", args[0]), requireNumber("max", args[1]));
        }
    )});

    globals->define("sin", Value{std::make_shared<NativeFunction>(
        "sin", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::sin(requireNumber("sin", args[0]));
        }
    )});

    globals->define("cos", Value{std::make_shared<NativeFunction>(
        "cos", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::cos(requireNumber("cos", args[0]));
        }
    )});

    globals->define("tan", Value{std::make_shared<NativeFunction>(
        "tan", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::tan(requireNumber("tan", args[0]));
        }
    )});

    globals->define("log", Value{std::make_shared<NativeFunction>(
        "log", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            double n = requireNumber("log", args[0]);
            if (n <= 0) throw nativeError("log", "Argument must be positive.");
            return std::log(n);
        }
    )});

    globals->define("log10", Value{std::make_shared<NativeFunction>(
        "log10", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            double n = requireNumber("log10", args[0]);
            if (n <= 0) throw nativeError("log10", "Argument must be positive.");
            return std::log10(n);
        }
    )});

    // Constants: plain global values, not functions, so they're used as `PI` rather than `PI()`.
    globals->define("PI", Value{3.14159265358979323846});
    globals->define("E", Value{2.71828182845904523536});

    // random(): returns a float in [0, 1)
    globals->define("random", Value{std::make_shared<NativeFunction>(
        "random", 0,
        [](Interpreter&, std::vector<Value>&) -> Value {
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return dist(rng());
        }
    )});

    // randomInt(min, max): inclusive integer range
    globals->define("randomInt", Value{std::make_shared<NativeFunction>(
        "randomInt", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            int lo = static_cast<int>(requireNumber("randomInt", args[0]));
            int hi = static_cast<int>(requireNumber("randomInt", args[1]));
            if (lo > hi) throw nativeError("randomInt", "min must not be greater than max.");
            std::uniform_int_distribution<int> dist(lo, hi);
            return static_cast<double>(dist(rng()));
        }
    )});

    // setSeed(n): reseeds the shared RNG for reproducible random()/randomInt()  (e.g. deterministic tests, replayable game runs).
    globals->define("setSeed", Value{std::make_shared<NativeFunction>(
        "setSeed", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            unsigned int seed = static_cast<unsigned int>(requireNumber("setSeed", args[0]));
            rng().seed(seed);
            return std::monostate{};
        }
    )});
}
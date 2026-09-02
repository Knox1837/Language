// string_lib.cpp: len() also accepts numbers/bools/nil by stringifying first, so it never throws just for "wrong type" the way math functions do
// but (upper/lower/substring) require an actual string.

#include "string_lib.h"
#include "native_function.h"
#include <algorithm>
#include <cctype>

static const std::string& requireString(const std::string& fnName, const Value& v) {
    if (!std::holds_alternative<std::string>(v)) {
        throw nativeError(fnName, "Expected a string argument.");
    }
    return std::get<std::string>(v);
}

void registerStringLib(std::shared_ptr<Environment> globals) {
    globals->define("len", Value{std::make_shared<NativeFunction>(
        "len", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("len", args[0]);
            return static_cast<double>(s.size());
        }
    )});

    globals->define("upper", Value{std::make_shared<NativeFunction>(
        "upper", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            std::string s = requireString("upper", args[0]);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            return s;
        }
    )});

    globals->define("lower", Value{std::make_shared<NativeFunction>(
        "lower", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            std::string s = requireString("lower", args[0]);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return s;
        }
    )});

    // substring(s, start, end) — end-exclusive, like Python's s[start:end]
    globals->define("substring", Value{std::make_shared<NativeFunction>(
        "substring", 3,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("substring", args[0]);
            if (!std::holds_alternative<double>(args[1]) || !std::holds_alternative<double>(args[2])) {
                throw nativeError("substring", "start/end must be numbers.");
            }
            int start = static_cast<int>(std::get<double>(args[1]));
            int end = static_cast<int>(std::get<double>(args[2]));
            int len = static_cast<int>(s.size());
            if (start < 0 || end > len || start > end) {
                throw nativeError("substring", "Index out of range.");
            }
            return s.substr(start, end - start);
        }
    )});

    globals->define("str", Value{std::make_shared<NativeFunction>(
        "str", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return stringifyValue(args[0]); // reuses the interpreter's own print-formatting logic
        }
    )});
}
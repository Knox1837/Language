// string_lib.cpp: len() also accepts numbers/bools/nil by stringifying first, so it never throws just for "wrong type" the way math functions do
// but (upper/lower/substring) require an actual string.

#include "string_lib.h"
#include "native_function.h"
#include "../interpreter/array_object.h"
#include <algorithm>
#include <cctype>
#include <sstream>

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

    // charAt(s, i) — single character as a 1-length string
    globals->define("charAt", Value{std::make_shared<NativeFunction>(
        "charAt", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("charAt", args[0]);
            if (!std::holds_alternative<double>(args[1])) {
                throw nativeError("charAt", "Index must be a number.");
            }
            int i = static_cast<int>(std::get<double>(args[1]));
            if (i < 0 || i >= static_cast<int>(s.size())) {
                throw nativeError("charAt", "Index out of range.");
            }
            return std::string(1, s[i]);
        }
    )});

    // find(s, sub) — index of first occurrence, or -1 if not found
    globals->define("find", Value{std::make_shared<NativeFunction>(
        "find", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("find", args[0]);
            const std::string& sub = requireString("find", args[1]);
            size_t pos = s.find(sub);
            return pos == std::string::npos ? -1.0 : static_cast<double>(pos);
        }
    )});

    globals->define("startsWith", Value{std::make_shared<NativeFunction>(
        "startsWith", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("startsWith", args[0]);
            const std::string& prefix = requireString("startsWith", args[1]);
            return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
        }
    )});

    globals->define("endsWith", Value{std::make_shared<NativeFunction>(
        "endsWith", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("endsWith", args[0]);
            const std::string& suffix = requireString("endsWith", args[1]);
            return s.size() >= suffix.size() &&
                   s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
    )});

    // trim(s): strips leading/trailing whitespace (spaces, tabs, newlines)
    globals->define("trim", Value{std::make_shared<NativeFunction>(
        "trim", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("trim", args[0]);
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return std::string(""); // all whitespace
            size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }
    )});

    // replace(s, old, new) — replaces ALL occurrences of old with new
    globals->define("replace", Value{std::make_shared<NativeFunction>(
        "replace", 3,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            std::string s = requireString("replace", args[0]);
            const std::string& oldStr = requireString("replace", args[1]);
            const std::string& newStr = requireString("replace", args[2]);
            if (oldStr.empty()) throw nativeError("replace", "old string must not be empty.");

            std::string result;
            size_t pos = 0, prev = 0;
            while ((pos = s.find(oldStr, prev)) != std::string::npos) {
                result += s.substr(prev, pos - prev);
                result += newStr;
                prev = pos + oldStr.size();
            }
            result += s.substr(prev);
            return result;
        }
    )});

    // split(s, delimiter) — returns an array of strings.
    // split("", ...) or a delimiter not found both still return a valid
    // 1-element array, matching Python's str.split() behavior.
    globals->define("split", Value{std::make_shared<NativeFunction>(
        "split", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("split", args[0]);
            const std::string& delim = requireString("split", args[1]);
            auto result = std::make_shared<ArrayObject>();

            if (delim.empty()) {
                throw nativeError("split", "delimiter must not be empty.");
            }

            size_t start = 0, pos;
            while ((pos = s.find(delim, start)) != std::string::npos) {
                result->elements.push_back(s.substr(start, pos - start));
                start = pos + delim.size();
            }
            result->elements.push_back(s.substr(start)); // final piece after the last delimiter
            return result;
        }
    )});

    // join(arr, delimiter) — the inverse of split(); elements are
    // stringified with the same rules as str()/print
    globals->define("join", Value{std::make_shared<NativeFunction>(
        "join", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::shared_ptr<ArrayObject>>(args[0])) {
                throw nativeError("join", "First argument must be an array.");
            }
            const std::string& delim = requireString("join", args[1]);
            auto array = std::get<std::shared_ptr<ArrayObject>>(args[0]);

            std::string result;
            for (size_t i = 0; i < array->elements.size(); i++) {
                if (i > 0) result += delim;
                result += stringifyValue(array->elements[i]);
            }
            return result;
        }
    )});

    // toNumber(s) — parses a string into a number; error on invalid input
    globals->define("toNumber", Value{std::make_shared<NativeFunction>(
        "toNumber", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& s = requireString("toNumber", args[0]);
            try {
                size_t consumed;
                double n = std::stod(s, &consumed);
                if (consumed != s.size()) {
                    throw nativeError("toNumber", "Invalid number: '" + s + "'.");
                }
                return n;
            } catch (const std::invalid_argument&) {
                throw nativeError("toNumber", "Invalid number: '" + s + "'.");
            } catch (const std::out_of_range&) {
                throw nativeError("toNumber", "Number out of range: '" + s + "'.");
            }
        }
    )});
}
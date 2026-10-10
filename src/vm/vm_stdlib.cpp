// vm_stdlib.cpp: native functions for the VM. Names, arities and error messages match the tree-walker's stdlib (src/stdlib/) so both engines behave the same.
#include "vm_stdlib.h"
#include "vm_array.h"
#include "vm_array_lib.h"
#include "vm_map.h"
#include "vm_map_lib.h"
#include "vm_native.h"
#include "vm_closure.h"
#include "vm_function.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iostream>
#include <random>
#include <stdexcept>

// `visiting` holds the arrays and maps currently being printed, so one that (directly or indirectly) contains itself prints "[...]" / "{...}" at the repeat instead of recursing until the stack overflows.
static std::string stringifyImpl(const VMValue& value, std::vector<const void*>& visiting) {
    if (isVMNil(value)) return "nil";
    if (isVMBool(value)) return asVMBool(value) ? "true" : "false";
    if (isVMNumber(value)) {
        double d = asVMNumber(value);
        // Whole numbers print without a decimal part. The range check keeps the long long cast well-defined for huge values, NaN and infinity.
        if (std::isfinite(d) && std::fabs(d) < 9.0e18 && d == static_cast<long long>(d)) {
            return std::to_string(static_cast<long long>(d));
        }
        return std::to_string(d);
    }
    if (isVMString(value)) return asVMString(value);
    if (isVMClosure(value)) return "<fn " + asVMClosure(value)->function->name + ">";
    if (isVMFunction(value)) return "<fn " + asVMFunction(value)->name + ">";
    if (isVMNative(value)) return "<native fn " + asVMNative(value)->name + ">";
    if (isVMArray(value)) {
        auto array = asVMArray(value);
        if (std::find(visiting.begin(), visiting.end(), array.get()) != visiting.end()) return "[...]";
        visiting.push_back(array.get());
        // Elements print like top-level values (so strings appear WITHOUT quotes: [a, b]), matching the tree-walker.
        std::string out = "[";
        for (size_t i = 0; i < array->elements.size(); i++) {
            if (i > 0) out += ", ";
            out += stringifyImpl(array->elements[i], visiting);
        }
        out += "]";
        visiting.pop_back();
        return out;
    }
    if (isVMMap(value)) {
        auto map = asVMMap(value);
        if (std::find(visiting.begin(), visiting.end(), map.get()) != visiting.end()) return "{...}";
        visiting.push_back(map.get());
        // Like the tree-walker: keys are quoted, values print like top-level values, and entries come out in key-sorted order.
        std::string out = "{";
        bool first = true;
        for (const auto& entry : map->entries) {
            if (!first) out += ", ";
            first = false;
            out += "\"" + entry.first + "\": " + stringifyImpl(entry.second, visiting);
        }
        out += "}";
        visiting.pop_back();
        return out;
    }
    return "nil";
}

std::string stringifyVMValue(const VMValue& value) {
    std::vector<const void*> visiting;
    return stringifyImpl(value, visiting);
}

// helpers

using Globals = std::unordered_map<std::string, VMValue>;

static bool needNumber(const VMValue& v, std::string& error) {
    if (isVMNumber(v)) return true;
    error = "Expected a number argument.";
    return false;
}

static bool needString(const VMValue& v, std::string& error) {
    if (isVMString(v)) return true;
    error = "Expected a string argument.";
    return false;
}

static void define(Globals& globals, const std::string& name, int arity, VMNative::Fn fn) {
    globals[name] = VMValue{std::make_shared<VMNative>(name, arity, std::move(fn))};
}

// One-argument function that takes a number and returns a number through a plain C math function (no domain check; the few that need one are written out below).
static void defineMath1(Globals& globals, const std::string& name, double (*f)(double)) {
    define(globals, name, 1, [f](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error)) return false;
        result = f(asVMNumber(args[0]));
        return true;
    });
}

// One RNG shared by random()/randomInt() for the whole program run, like the tree-walker's.
static std::mt19937& rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

// math

static void registerMath(Globals& globals) {
    define(globals, "clock", 0, [](std::vector<VMValue>&, VMValue& result, std::string&) {
        result = static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
        return true;
    });

    defineMath1(globals, "abs",   [](double x) { return std::fabs(x); });
    defineMath1(globals, "floor", [](double x) { return std::floor(x); });
    defineMath1(globals, "ceil",  [](double x) { return std::ceil(x); });
    defineMath1(globals, "round", [](double x) { return std::round(x); });
    defineMath1(globals, "sin",   [](double x) { return std::sin(x); });
    defineMath1(globals, "cos",   [](double x) { return std::cos(x); });
    defineMath1(globals, "tan",   [](double x) { return std::tan(x); });

    define(globals, "sqrt", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error)) return false;
        double n = asVMNumber(args[0]);
        if (n < 0) {
            error = "Cannot take the square root of a negative number.";
            return false;
        }
        result = std::sqrt(n);
        return true;
    });

    define(globals, "pow", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error) || !needNumber(args[1], error)) return false;
        result = std::pow(asVMNumber(args[0]), asVMNumber(args[1]));
        return true;
    });

    define(globals, "min", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error) || !needNumber(args[1], error)) return false;
        result = std::min(asVMNumber(args[0]), asVMNumber(args[1]));
        return true;
    });

    define(globals, "max", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error) || !needNumber(args[1], error)) return false;
        result = std::max(asVMNumber(args[0]), asVMNumber(args[1]));
        return true;
    });

    define(globals, "log", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error)) return false;
        double n = asVMNumber(args[0]);
        if (n <= 0) {
            error = "Argument must be positive.";
            return false;
        }
        result = std::log(n);
        return true;
    });

    define(globals, "log10", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error)) return false;
        double n = asVMNumber(args[0]);
        if (n <= 0) {
            error = "Argument must be positive.";
            return false;
        }
        result = std::log10(n);
        return true;
    });

    // Constants: plain global values, not functions, so they're used as `PI` rather than `PI()`.
    globals["PI"] = VMValue{3.14159265358979323846};
    globals["E"] = VMValue{2.71828182845904523536};

    // random(): a float in [0, 1)
    define(globals, "random", 0, [](std::vector<VMValue>&, VMValue& result, std::string&) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        result = dist(rng());
        return true;
    });

    // randomInt(min, max): inclusive integer range
    define(globals, "randomInt", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error) || !needNumber(args[1], error)) return false;
        int lo = static_cast<int>(asVMNumber(args[0]));
        int hi = static_cast<int>(asVMNumber(args[1]));
        if (lo > hi) {
            error = "min must not be greater than max.";
            return false;
        }
        std::uniform_int_distribution<int> dist(lo, hi);
        result = static_cast<double>(dist(rng()));
        return true;
    });

    // setSeed(n): reseeds the shared RNG so random()/randomInt() become reproducible
    define(globals, "setSeed", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error)) return false;
        rng().seed(static_cast<unsigned int>(asVMNumber(args[0])));
        result = std::monostate{};
        return true;
    });
}

// string

static void registerString(Globals& globals) {
    define(globals, "len", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error)) return false;
        result = static_cast<double>(asVMString(args[0]).size());
        return true;
    });

    define(globals, "str", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = stringifyVMValue(args[0]);
        return true;
    });

    define(globals, "upper", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error)) return false;
        std::string s = asVMString(args[0]);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        result = std::move(s);
        return true;
    });

    define(globals, "lower", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error)) return false;
        std::string s = asVMString(args[0]);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        result = std::move(s);
        return true;
    });

    // substring(s, start, end): end-exclusive, like Python's s[start:end]
    define(globals, "substring", 3, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error)) return false;
        if (!isVMNumber(args[1]) || !isVMNumber(args[2])) {
            error = "start/end must be numbers.";
            return false;
        }
        const std::string& s = asVMString(args[0]);
        int start = static_cast<int>(asVMNumber(args[1]));
        int end = static_cast<int>(asVMNumber(args[2]));
        int len = static_cast<int>(s.size());
        if (start < 0 || end > len || start > end) {
            error = "Index out of range.";
            return false;
        }
        result = s.substr(start, end - start);
        return true;
    });

    // charAt(s, i): a single character as a 1-length string
    define(globals, "charAt", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error)) return false;
        if (!isVMNumber(args[1])) {
            error = "Index must be a number.";
            return false;
        }
        const std::string& s = asVMString(args[0]);
        int i = static_cast<int>(asVMNumber(args[1]));
        if (i < 0 || i >= static_cast<int>(s.size())) {
            error = "Index out of range.";
            return false;
        }
        result = std::string(1, s[i]);
        return true;
    });

    // find(s, sub): index of the first occurrence, or -1 if not found
    define(globals, "find", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error) || !needString(args[1], error)) return false;
        size_t pos = asVMString(args[0]).find(asVMString(args[1]));
        result = pos == std::string::npos ? -1.0 : static_cast<double>(pos);
        return true;
    });

    define(globals, "startsWith", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error) || !needString(args[1], error)) return false;
        const std::string& s = asVMString(args[0]);
        const std::string& prefix = asVMString(args[1]);
        result = s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
        return true;
    });

    define(globals, "endsWith", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error) || !needString(args[1], error)) return false;
        const std::string& s = asVMString(args[0]);
        const std::string& suffix = asVMString(args[1]);
        result = s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        return true;
    });

    // trim(s): strips leading/trailing whitespace (spaces, tabs, carriage returns, newlines)
    define(globals, "trim", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error)) return false;
        const std::string& s = asVMString(args[0]);
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            result = std::string(""); // all whitespace
            return true;
        }
        size_t end = s.find_last_not_of(" \t\r\n");
        result = s.substr(start, end - start + 1);
        return true;
    });

    // replace(s, old, new): replaces ALL occurrences of old with new
    define(globals, "replace", 3, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error) || !needString(args[1], error) || !needString(args[2], error)) return false;
        const std::string& s = asVMString(args[0]);
        const std::string& oldStr = asVMString(args[1]);
        const std::string& newStr = asVMString(args[2]);
        if (oldStr.empty()) {
            error = "old string must not be empty.";
            return false;
        }
        std::string out;
        size_t pos = 0, prev = 0;
        while ((pos = s.find(oldStr, prev)) != std::string::npos) {
            out += s.substr(prev, pos - prev);
            out += newStr;
            prev = pos + oldStr.size();
        }
        out += s.substr(prev);
        result = std::move(out);
        return true;
    });

    // split(s, delimiter): an array of the pieces. A delimiter that never occurs still gives a 1-element array (Python-style); an empty delimiter is an error.
    define(globals, "split", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error) || !needString(args[1], error)) return false;
        const std::string& s = asVMString(args[0]);
        const std::string& delim = asVMString(args[1]);
        if (delim.empty()) {
            error = "delimiter must not be empty.";
            return false;
        }
        auto out = std::make_shared<VMArray>();
        size_t start = 0, pos;
        while ((pos = s.find(delim, start)) != std::string::npos) {
            out->elements.push_back(s.substr(start, pos - start));
            start = pos + delim.size();
        }
        out->elements.push_back(s.substr(start)); // the final piece after the last delimiter
        result = out;
        return true;
    });

    // join(arr, delimiter): the inverse of split(); elements are stringified exactly like str()/print
    define(globals, "join", 2, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!isVMArray(args[0])) {
            error = "First argument must be an array.";
            return false;
        }
        if (!needString(args[1], error)) return false;
        const std::string& delim = asVMString(args[1]);
        auto array = asVMArray(args[0]);
        std::string out;
        for (size_t i = 0; i < array->elements.size(); i++) {
            if (i > 0) out += delim;
            out += stringifyVMValue(array->elements[i]);
        }
        result = std::move(out);
        return true;
    });

    // toNumber(s): parses a string into a number; an error on invalid input
    define(globals, "toNumber", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error)) return false;
        const std::string& s = asVMString(args[0]);
        try {
            size_t consumed;
            double n = std::stod(s, &consumed);
            if (consumed != s.size()) {
                error = "Invalid number: '" + s + "'.";
                return false;
            }
            result = n;
            return true;
        } catch (const std::invalid_argument&) {
            error = "Invalid number: '" + s + "'.";
            return false;
        } catch (const std::out_of_range&) {
            error = "Number out of range: '" + s + "'.";
            return false;
        }
    });
}

// type

static void registerType(Globals& globals) {
    define(globals, "isNumber", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = isVMNumber(args[0]);
        return true;
    });

    define(globals, "isString", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = isVMString(args[0]);
        return true;
    });

    define(globals, "isBool", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = isVMBool(args[0]);
        return true;
    });

    define(globals, "isArray", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = isVMArray(args[0]);
        return true;
    });

    define(globals, "isMap", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = isVMMap(args[0]);
        return true;
    });

    define(globals, "isNil", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = isVMNil(args[0]);
        return true;
    });

    // Covers both user functions (closures) and native functions, like the tree-walker's isFunction (which accepts any Callable).
    define(globals, "isFunction", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = isVMClosure(args[0]) || isVMNative(args[0]);
        return true;
    });
}

// io

static void registerIo(Globals& globals) {
    // input(): reads a full line from stdin. Returns "" (not an error) on EOF or stream failure, so a script can loop until it sees an empty string.
    define(globals, "input", 0, [](std::vector<VMValue>&, VMValue& result, std::string&) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            result = std::string("");
            return true;
        }
        result = std::move(line);
        return true;
    });
}

void registerVMStdlib(std::unordered_map<std::string, VMValue>& globals) {
    registerMath(globals);
    registerString(globals);
    registerType(globals);
    registerIo(globals);
    registerVMArrayLib(globals); // push, pop, length, ... (the array functions live in their own file)
    registerVMMapLib(globals);   // keys, values, hasKey, remove
}
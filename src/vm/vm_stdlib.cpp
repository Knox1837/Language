// vm_stdlib.cpp: native functions for the VM. Names, arities and error messages match the tree-walker's stdlib so both engines behave the same.
#include "vm_stdlib.h"
#include "vm_native.h"
#include "vm_closure.h"
#include "vm_function.h"
#include <cmath>
#include <ctime>

std::string stringifyVMValue(const VMValue& value) {
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
    return "nil";
}

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

static void define(std::unordered_map<std::string, VMValue>& globals, const std::string& name, int arity, VMNative::Fn fn) {
    globals[name] = VMValue{std::make_shared<VMNative>(name, arity, std::move(fn))};
}

void registerVMStdlib(std::unordered_map<std::string, VMValue>& globals) {
    define(globals, "clock", 0, [](std::vector<VMValue>&, VMValue& result, std::string&) {
        result = static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
        return true;
    });

    define(globals, "abs", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error)) return false;
        result = std::fabs(asVMNumber(args[0]));
        return true;
    });

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

    define(globals, "floor", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needNumber(args[0], error)) return false;
        result = std::floor(asVMNumber(args[0]));
        return true;
    });

    define(globals, "len", 1, [](std::vector<VMValue>& args, VMValue& result, std::string& error) {
        if (!needString(args[0], error)) return false;
        result = static_cast<double>(asVMString(args[0]).size());
        return true;
    });

    define(globals, "str", 1, [](std::vector<VMValue>& args, VMValue& result, std::string&) {
        result = stringifyVMValue(args[0]);
        return true;
    });
}
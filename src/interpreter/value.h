// Defines Value (reuses the same variant as AST literals) plus isTruthy() and stringifyValue() helpers
#pragma once
#include "../ast/expr.h"
using Value = LiteralValue;

// True if the value is "truthy" for if/while/and/or purposes.
// This language's rule: nil and false are falsey, everything else truthy
// (matches Lox/Ruby-style truthiness, not C's "0 is false").
inline bool isTruthy(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return false;
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value);
    return true;
}

// Converts a runtime value to its printable string form (used by `print`).
inline std::string stringifyValue(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return "nil";
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
    if (std::holds_alternative<double>(value)) {
        double d = std::get<double>(value);
        // Print whole numbers without a trailing ".0" (e.g. "10" not "10.0")
        if (d == static_cast<long long>(d)) {
            return std::to_string(static_cast<long long>(d));
        }
        return std::to_string(d);
    }
    return std::get<std::string>(value);
}
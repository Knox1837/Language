// Defines Value (reuses the same variant as AST literals) plus isTruthy() and stringifyValue() helpers
#pragma once
#include <memory>
#include "../ast/expr.h"

class Callable; // defined in callable.h; forward-declared so Value can hold a pointer to it

using Value = std::variant<std::monostate, double, bool, std::string, std::shared_ptr<Callable>>;

// Converts a parse-time LiteralValue (nil/number/bool/string only) into a runtime Value (which has one extra alternative for functions). 
// Needed because the two variants have different alternative sets, so C++ won't convert between them implicitly.
inline Value fromLiteral(const LiteralValue& lit) {
    return std::visit([](auto&& v) -> Value { return v; }, lit);
}

// True if the value is "truthy" for if/while/and/or purposes.
inline bool isTruthy(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return false;
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value);
    return true;
}

// Converts a runtime value to its printable string form (used by `print`).
std::string stringifyValue(const Value& value);
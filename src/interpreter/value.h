#pragma once
// value.h: the runtime Value type. 
// Extends the AST's LiteralValue with two more alternatives: a callable (function/class) and an instance.
// A Value is always one of nil / number / bool / string / callable / instance.

#include <memory>
#include "../ast/expr.h"

class Callable;     // callable.h — functions AND classes (calling a class constructs an instance)
class LoxInstance;   // lox_instance.h — a runtime object with fields, created from a class
class ArrayObject;   // array_object.h — a runtime array, holding a vector<Value>

using Value = std::variant<std::monostate, double, bool, std::string,
                            std::shared_ptr<Callable>, std::shared_ptr<LoxInstance>,
                            std::shared_ptr<ArrayObject>>;

// Converts a parse-time LiteralValue (nil/number/bool/string only) into a runtime Value (which has extra alternatives for functions/instances).
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
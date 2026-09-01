// value.cpp: stringifyValue is defined here (not in value.h) because it
// needs Callable::toString(), and callable.h can't be included from value.h without a circular include (callable.h includes value.h)

#include "value.h"
#include "callable.h"
#include "lox_instance.h"

std::string stringifyValue(const Value& value) {
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
    if (std::holds_alternative<std::shared_ptr<Callable>>(value)) {
        return std::get<std::shared_ptr<Callable>>(value)->toString();
    }
    if (std::holds_alternative<std::shared_ptr<LoxInstance>>(value)) {
        return std::get<std::shared_ptr<LoxInstance>>(value)->toString();
    }
    return std::get<std::string>(value);
}
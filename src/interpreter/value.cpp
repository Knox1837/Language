// value.cpp: stringifyValue is defined here (not in value.h) because it
// needs Callable::toString(), and callable.h can't be included from value.h without a circular include (callable.h includes value.h)
#include "value.h"
#include "callable.h"
#include "lox_instance.h"
#include "array_object.h"
#include "map_object.h"

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
    if (std::holds_alternative<std::shared_ptr<ArrayObject>>(value)) {
        auto& elements = std::get<std::shared_ptr<ArrayObject>>(value)->elements;
        std::string out = "[";
        for (size_t i = 0; i < elements.size(); i++) {
            if (i > 0) out += ", ";
            out += stringifyValue(elements[i]);
        }
        out += "]";
        return out;
    }
    if (std::holds_alternative<std::shared_ptr<MapObject>>(value)) {
        auto& entries = std::get<std::shared_ptr<MapObject>>(value)->entries;
        std::string out = "{";
        bool first = true;
        for (auto& [key, val] : entries) {
            if (!first) out += ", ";
            first = false;
            out += "\"" + key + "\": " + stringifyValue(val);
        }
        out += "}";
        return out;
    }
    return std::get<std::string>(value);
}
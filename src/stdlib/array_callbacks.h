// array_callbacks.h: the array operations that take a FUNCTION (map, filter, reduce, and sort with a comparator), and the plain sort(arr).
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include "native_function.h"
#include "../interpreter/array_object.h"
#include "../interpreter/callable.h"
#include "../util/tim_sort.h"

namespace arraycb {

inline std::shared_ptr<Callable> requireFunction(const std::string& fnName, const Value& value) {
    if (!std::holds_alternative<std::shared_ptr<Callable>>(value)) {
        throw nativeError(fnName, "Expected a function argument.");
    }
    return std::get<std::shared_ptr<Callable>>(value);
}

inline Value callWith(Interpreter& interpreter, const std::string& fnName, const std::shared_ptr<Callable>& fn, std::vector<Value> args) {
    if (static_cast<int>(args.size()) < fn->arity() || static_cast<int>(args.size()) > fn->maxArity()) {
        throw nativeError(fnName, arityMessage(fn->arity(), fn->maxArity(), args.size()));
    }
    return fn->call(interpreter, args);
}

// map(arr, f): a NEW array of f(element) for every element.
inline Value map(Interpreter& interpreter, const std::shared_ptr<ArrayObject>& array, const Value& fnValue) {
    auto fn = requireFunction("map", fnValue);
    std::vector<Value> items = array->elements;
    auto out = std::make_shared<ArrayObject>();
    out->elements.reserve(items.size());
    for (const Value& item : items) {
        out->elements.push_back(callWith(interpreter, "map", fn, {item}));
    }
    return out;
}

// filter(arr, f): a NEW array of the elements for which f(element) is truthy.
inline Value filter(Interpreter& interpreter, const std::shared_ptr<ArrayObject>& array, const Value& fnValue) {
    auto fn = requireFunction("filter", fnValue);
    std::vector<Value> items = array->elements;
    auto out = std::make_shared<ArrayObject>();
    for (const Value& item : items) {
        if (isTruthy(callWith(interpreter, "filter", fn, {item}))) out->elements.push_back(item);
    }
    return out;
}

// reduce(arr, f, initial): folds left to right: acc = initial; acc = f(acc, element) for each element; returns acc. An empty array gives `initial`.
inline Value reduce(Interpreter& interpreter, const std::shared_ptr<ArrayObject>& array, const Value& fnValue, const Value& initial) {
    auto fn = requireFunction("reduce", fnValue);
    std::vector<Value> items = array->elements;
    Value accumulator = initial;
    for (const Value& item : items) {
        accumulator = callWith(interpreter, "reduce", fn, {accumulator, item});
    }
    return accumulator;
}

// sort(arr, cmp): sorts IN PLACE with Timsort (stable), and returns the same array. cmp(a, b) returns a number: negative if a belongs before b, positive if after, zero if they are equal (their order is kept).
// The sort runs on a copy that replaces the array only when it finishes, so an error in the comparator leaves the array exactly as it was.
inline Value sortWith(Interpreter& interpreter, const std::shared_ptr<ArrayObject>& array, const Value& cmpValue) {
    auto fn = requireFunction("sort", cmpValue);
    std::vector<Value> items = array->elements;
    timSort(items, [&](const Value& a, const Value& b) {
        Value verdict = callWith(interpreter, "sort", fn, {a, b});
        if (!std::holds_alternative<double>(verdict)) {
            throw nativeError("sort", "Comparator must return a number.");
        }
        return std::get<double>(verdict) < 0; // Timsort only asks "does a come strictly before b?"
    });
    array->elements = std::move(items);
    return array;
}

// sort(arr): ascending, IN PLACE, stable, for an array of all numbers or all strings (anything else is an error, found before anything moves). NaN sorts after every other number, which keeps this a valid ordering.
inline Value sortNatural(const std::shared_ptr<ArrayObject>& array) {
    auto& elements = array->elements;
    if (elements.size() >= 2) {
        bool allNumbers = true, allStrings = true;
        for (const Value& element : elements) {
            allNumbers = allNumbers && std::holds_alternative<double>(element);
            allStrings = allStrings && std::holds_alternative<std::string>(element);
        }
        if (!allNumbers && !allStrings) throw nativeError("sort", "Can only sort arrays of all-numbers or all-strings.");
        timSort(elements, [](const Value& a, const Value& b) {
            if (std::holds_alternative<double>(a)) {
                const double x = std::get<double>(a), y = std::get<double>(b);
                if (std::isnan(x)) return false;
                if (std::isnan(y)) return true;
                return x < y;
            }
            return std::get<std::string>(a) < std::get<std::string>(b);
        });
    }
    return array;
}

} // namespace arraycb
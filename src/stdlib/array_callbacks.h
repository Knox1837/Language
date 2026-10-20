// array_callbacks.h: the array operations that take a FUNCTION: map, filter, reduce, and sort with a comparator.
// Written once here and used by both forms of each: the free function (map(arr, f), in array_lib.cpp) and the bound method (arr.map(f), in array_methods.cpp).
//
// How a callback is called: a native function receives the Interpreter, so it calls a script function like any Callable: callable->call(interpreter, args). A runtime error inside the callback is a C++ exception that simply passes through the native on its way to the top level, which reports it once.
// The callback is called with exactly the arguments documented for each operation (map/filter: the element; reduce: accumulator, element; sort: two elements). A function that takes a different number is an arity error, reported like any wrong-arity call.
// Every operation works on a SNAPSHOT of the elements, so a callback that modifies the array being processed (pushes to it, say) cannot invalidate the loop.
#pragma once
#include <string>
#include <vector>
#include "native_function.h"
#include "../interpreter/array_object.h"
#include "../interpreter/callable.h"
#include "../util/merge_sort.h"

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

// sort(arr, cmp): sorts IN PLACE, stably, and returns the same array. cmp(a, b) returns a number: negative if a belongs before b, positive if after, zero if they are equal (their order is kept).
// The sort runs on a copy that replaces the array only when it finishes, so an error in the comparator leaves the array exactly as it was.
inline Value sortWith(Interpreter& interpreter, const std::shared_ptr<ArrayObject>& array, const Value& cmpValue) {
    auto fn = requireFunction("sort", cmpValue);
    std::vector<Value> items = array->elements;
    stableMergeSort(items, [&](const Value& a, const Value& b) {
        Value verdict = callWith(interpreter, "sort", fn, {a, b});
        if (!std::holds_alternative<double>(verdict)) {
            throw nativeError("sort", "Comparator must return a number.");
        }
        return std::get<double>(verdict) > 0;
    });
    array->elements = std::move(items);
    return array;
}

} // namespace arraycb
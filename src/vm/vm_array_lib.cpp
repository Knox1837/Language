// vm_array_lib.cpp: array operations, written ONCE and exposed two ways, like the tree-walker's array_lib.cpp (free functions) and array_methods.cpp (bound methods) combined.
#include "vm_array_lib.h"
#include "vm_array.h"
#include "vm_map.h"
#include "vm_native.h"
#include <algorithm>
#include <cmath>

namespace {

using ArrayPtr = std::shared_ptr<VMArray>;

// One array operation. `rest` holds the arguments AFTER the array, so push(arr, x) and arr.push(x) both see {x}.
using ArrayOp = bool (*)(const ArrayPtr& array, std::vector<VMValue>& rest, VMValue& result, std::string& error);

struct ArrayOpInfo {
    const char* name;
    int arity; // counts `rest` only: the free-function form takes one more (the array itself)
    ArrayOp fn;
};

const char* const kOrderError = "Can only sort arrays of all-numbers or all-strings.";

// Truncates toward zero like the tree-walker's static_cast<int>, but reports NaN, infinity and absurdly large values as "not a usable index" instead of invoking undefined behavior.
bool truncToLong(double d, long long& out) {
    if (!std::isfinite(d) || std::fabs(d) >= 9.0e18) return false;
    out = static_cast<long long>(d);
    return true;
}

// Ordering used by sort() and binarySearch(): numbers with numbers, strings with strings, nothing else.
bool comparable(const VMValue& a, const VMValue& b) {
    return (isVMNumber(a) && isVMNumber(b)) || (isVMString(a) && isVMString(b));
}

// NaN sorts after every other number, which keeps this a valid strict weak ordering (std::sort misbehaves badly with one that isn't).
// Anything that isn't a comparable pair is simply "not less", so this is safe to call on any two values.
bool lessThan(const VMValue& a, const VMValue& b) {
    if (isVMNumber(a) && isVMNumber(b)) {
        double x = asVMNumber(a), y = asVMNumber(b);
        if (std::isnan(x)) return false;
        if (std::isnan(y)) return true;
        return x < y;
    }
    if (isVMString(a) && isVMString(b)) return asVMString(a) < asVMString(b);
    return false;
}

bool opPush(const ArrayPtr& array, std::vector<VMValue>& rest, VMValue& result, std::string&) {
    array->elements.push_back(rest[0]);
    result = static_cast<double>(array->elements.size());
    return true;
}

bool opPop(const ArrayPtr& array, std::vector<VMValue>&, VMValue& result, std::string& error) {
    if (array->elements.empty()) {
        error = "Cannot pop from an empty array.";
        return false;
    }
    result = std::move(array->elements.back());
    array->elements.pop_back();
    return true;
}

bool opLength(const ArrayPtr& array, std::vector<VMValue>&, VMValue& result, std::string&) {
    result = static_cast<double>(array->elements.size());
    return true;
}

bool opContains(const ArrayPtr& array, std::vector<VMValue>& rest, VMValue& result, std::string&) {
    for (const auto& element : array->elements) {
        if (element == rest[0]) {
            result = true;
            return true;
        }
    }
    result = false;
    return true;
}

bool opIndexOf(const ArrayPtr& array, std::vector<VMValue>& rest, VMValue& result, std::string&) {
    for (size_t i = 0; i < array->elements.size(); i++) {
        if (array->elements[i] == rest[0]) {
            result = static_cast<double>(i);
            return true;
        }
    }
    result = -1.0;
    return true;
}

// Ascending, IN PLACE; returns the same array so calls can chain (arr.sort().reverse()).
bool opSort(const ArrayPtr& array, std::vector<VMValue>&, VMValue& result, std::string& error) {
    auto& elements = array->elements;
    // With fewer than two elements there is nothing to compare, so (like the tree-walker) any element type is accepted and there is nothing to do.
    if (elements.size() >= 2) {
        // Natives report errors by return value, not by throwing, so check the whole array BEFORE sorting instead of failing from inside std::sort's comparator.
        bool allNumbers = true, allStrings = true;
        for (const auto& element : elements) {
            allNumbers = allNumbers && isVMNumber(element);
            allStrings = allStrings && isVMString(element);
        }
        if (!allNumbers && !allStrings) {
            error = kOrderError;
            return false;
        }
        std::sort(elements.begin(), elements.end(), [](const VMValue& a, const VMValue& b) { return lessThan(a, b); });
    }
    result = array;
    return true;
}

bool opReverse(const ArrayPtr& array, std::vector<VMValue>&, VMValue& result, std::string&) {
    std::reverse(array->elements.begin(), array->elements.end());
    result = array;
    return true;
}

// slice(start, end): a NEW array of [start, end), end-exclusive like substring(); the original is untouched.
bool opSlice(const ArrayPtr& array, std::vector<VMValue>& rest, VMValue& result, std::string& error) {
    if (!isVMNumber(rest[0]) || !isVMNumber(rest[1])) {
        error = "start/end must be numbers.";
        return false;
    }
    long long start = 0, end = 0;
    long long len = static_cast<long long>(array->elements.size());
    if (!truncToLong(asVMNumber(rest[0]), start) || !truncToLong(asVMNumber(rest[1]), end) ||
        start < 0 || end > len || start > end) {
        error = "Index out of range.";
        return false;
    }
    auto out = std::make_shared<VMArray>();
    out->elements.assign(array->elements.begin() + start, array->elements.begin() + end);
    result = out;
    return true;
}

// binarySearch(value): assumes the array is already sorted ascending. Index of a match, or -1.
bool opBinarySearch(const ArrayPtr& array, std::vector<VMValue>& rest, VMValue& result, std::string& error) {
    const auto& elements = array->elements;
    long long lo = 0, hi = static_cast<long long>(elements.size()) - 1;
    while (lo <= hi) {
        long long mid = lo + (hi - lo) / 2;
        if (elements[mid] == rest[0]) {
            result = static_cast<double>(mid);
            return true;
        }
        if (!comparable(elements[mid], rest[0])) {
            error = kOrderError;
            return false;
        }
        if (lessThan(elements[mid], rest[0])) lo = mid + 1;
        else hi = mid - 1;
    }
    result = -1.0;
    return true;
}

const ArrayOpInfo kOps[] = {
    {"push",         1, opPush},
    {"pop",          0, opPop},
    {"length",       0, opLength},
    {"contains",     1, opContains},
    {"indexOf",      1, opIndexOf},
    {"sort",         0, opSort},
    {"reverse",      0, opReverse},
    {"slice",        2, opSlice},
    {"binarySearch", 1, opBinarySearch},
};

} // namespace

void registerVMArrayLib(std::unordered_map<std::string, VMValue>& globals) {
    for (const auto& op : kOps) {
        ArrayOp fn = op.fn;
        // length() is the one free function that will also accept maps, so its message differs.
        bool isLength = std::string(op.name) == "length";
        globals[op.name] = VMValue{std::make_shared<VMNative>(
            op.name, op.arity + 1,
            [fn, isLength](std::vector<VMValue>& args, VMValue& result, std::string& error) {
                if (isLength && isVMMap(args[0])) {
                    result = static_cast<double>(asVMMap(args[0])->entries.size());
                    return true;
                }
                if (!isVMArray(args[0])) {
                    error = isLength ? "Expected an array or map argument." : "Expected an array argument.";
                    return false;
                }
                std::vector<VMValue> rest(args.begin() + 1, args.end());
                return fn(asVMArray(args[0]), rest, result, error);
            })};
    }
}

bool getVMArrayMethod(const std::shared_ptr<VMArray>& array, const std::string& name, VMValue& result) {
    for (const auto& op : kOps) {
        if (name == op.name) {
            ArrayOp fn = op.fn;
            // The lambda captures the array, so the returned native is permanently bound to this instance: `var p = a.push; p(1);` appends to `a`.
            result = VMValue{std::make_shared<VMNative>(
                op.name, op.arity,
                [array, fn](std::vector<VMValue>& args, VMValue& out, std::string& error) {
                    return fn(array, args, out, error);
                })};
            return true;
        }
    }
    return false;
}
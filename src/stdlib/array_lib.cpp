// array_lib.cpp: implements the built-in array functions (push, pop, length, contains, indexOf, sort, reverse, slice)
#include "array_lib.h"
#include "native_function.h"
#include "../interpreter/array_object.h"
#include <algorithm>

static std::shared_ptr<ArrayObject> requireArray(const std::string& fnName, const Value& v) {
    if (!std::holds_alternative<std::shared_ptr<ArrayObject>>(v)) {
        throw nativeError(fnName, "Expected an array argument.");
    }
    return std::get<std::shared_ptr<ArrayObject>>(v);
}

// Ascending comparator used by sort(). 
static bool ascending(const Value& a, const Value& b, const std::string& fnName) {
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        return std::get<double>(a) < std::get<double>(b);
    }
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) < std::get<std::string>(b);
    }
    throw nativeError(fnName, "Can only sort arrays of all-numbers or all-strings.");
}

void registerArrayLib(std::shared_ptr<Environment> globals) {
    // push(arr, value) — appends in place, returns the array's new length
    globals->define("push", Value{std::make_shared<NativeFunction>(
        "push", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("push", args[0]);
            array->elements.push_back(args[1]);
            return static_cast<double>(array->elements.size());
        }
    )});

    // pop(arr): removes and returns the last element; error if empty
    globals->define("pop", Value{std::make_shared<NativeFunction>(
        "pop", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("pop", args[0]);
            if (array->elements.empty()) {
                throw nativeError("pop", "Cannot pop from an empty array.");
            }
            Value last = array->elements.back();
            array->elements.pop_back();
            return last;
        }
    )});

    // length(arr): returns number of elements 
    globals->define("length", Value{std::make_shared<NativeFunction>(
        "length", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("length", args[0]);
            return static_cast<double>(array->elements.size());
        }
    )});

    // contains(arr, value) — true if value appears anywhere in the array
    globals->define("contains", Value{std::make_shared<NativeFunction>(
        "contains", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("contains", args[0]);
            for (auto& element : array->elements) {
                if (element == args[1]) return true;
            }
            return false;
        }
    )});

    // indexOf(arr, value) — first matching index, or -1 if not found
    globals->define("indexOf", Value{std::make_shared<NativeFunction>(
        "indexOf", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("indexOf", args[0]);
            for (size_t i = 0; i < array->elements.size(); i++) {
                if (array->elements[i] == args[1]) return static_cast<double>(i);
            }
            return -1.0;
        }
    )});

    // sort(arr) — ascending sort IN PLACE; numbers or strings only (see ascending())
    // uses introsort O(n log n) with std::sort, which is a hybrid of quicksort, heapsort, and insertion sort.
    globals->define("sort", Value{std::make_shared<NativeFunction>(
        "sort", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("sort", args[0]);
            std::sort(array->elements.begin(), array->elements.end(),
                      [](const Value& a, const Value& b) { return ascending(a, b, "sort"); });
            return args[0]; // returns the same array, for chaining like print(sort(arr))
        }
    )});

    // reverse(arr) — reverses IN PLACE, returns the same array
    globals->define("reverse", Value{std::make_shared<NativeFunction>(
        "reverse", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("reverse", args[0]);
            std::reverse(array->elements.begin(), array->elements.end());
            return args[0];
        }
    )});

    // slice(arr, start, end): returns a NEW array with elements [start, end), end-exclusive like substring(). Does not mutate the original.
    globals->define("slice", Value{std::make_shared<NativeFunction>(
        "slice", 3,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("slice", args[0]);
            if (!std::holds_alternative<double>(args[1]) || !std::holds_alternative<double>(args[2])) {
                throw nativeError("slice", "start/end must be numbers.");
            }
            int start = static_cast<int>(std::get<double>(args[1]));
            int end = static_cast<int>(std::get<double>(args[2]));
            int len = static_cast<int>(array->elements.size());
            if (start < 0 || end > len || start > end) {
                throw nativeError("slice", "Index out of range.");
            }
            auto result = std::make_shared<ArrayObject>();
            result->elements.assign(array->elements.begin() + start, array->elements.begin() + end);
            return result;
        }
    )});
    // binarySearch(arr, value): assumes arr is already sorted ascending 
    // Returns the matching index, or -1 if not found.
    globals->define("binarySearch", Value{std::make_shared<NativeFunction>(
        "binarySearch", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto array = requireArray("binarySearch", args[0]);
            int lo = 0, hi = static_cast<int>(array->elements.size()) - 1;
 
            while (lo <= hi) {
                int mid = lo + (hi - lo) / 2;
                if (array->elements[mid] == args[1]) return static_cast<double>(mid);
                if (ascending(array->elements[mid], args[1], "binarySearch")) {
                    lo = mid + 1;
                } else {
                    hi = mid - 1;
                }
            }
            return -1.0;
        }
    )});

}
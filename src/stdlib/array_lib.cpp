// array_lib.cpp: arrays have reference semantics 
// (Value holds a shared_ptr<ArrayObject>), so push()/pop() mutate the array in place
// callers see the change through any other variable referencing the ame array, same as objects/instances already behave

#include "array_lib.h"
#include "native_function.h"
#include "../interpreter/array_object.h"

static std::shared_ptr<ArrayObject> requireArray(const std::string& fnName, const Value& v) {
    if (!std::holds_alternative<std::shared_ptr<ArrayObject>>(v)) {
        throw nativeError(fnName, "Expected an array argument.");
    }
    return std::get<std::shared_ptr<ArrayObject>>(v);
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

    // length(arr): number of elements 
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
}
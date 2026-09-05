// array_methods.cpp: one block per method name
// Each returns Native function whose lamda captures array by value, so the method is already bound to that specific array instance.
#include "array_methods.h"
#include "array_object.h"
#include "environment.h" // for RuntimeError
#include "../stdlib/native_function.h"
#include <algorithm>

static bool ascending(const Value& a, const Value& b, const std::string& fnName) {
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        return std::get<double>(a) < std::get<double>(b);
    }
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) < std::get<std::string>(b);
    }
    throw nativeError(fnName, "Can only sort arrays of all-numbers or all-strings.");
}

Value getArrayMethod(std::shared_ptr<ArrayObject> array, const Token& name) {
    const std::string& m = name.lexeme;

    if (m == "push") {
        return std::make_shared<NativeFunction>("push", 1,
            [array](Interpreter&, std::vector<Value>& args) -> Value {
                array->elements.push_back(args[0]);
                return static_cast<double>(array->elements.size());
            });
    }
    if (m == "pop") {
        return std::make_shared<NativeFunction>("pop", 0,
            [array](Interpreter&, std::vector<Value>&) -> Value {
                if (array->elements.empty()) throw nativeError("pop", "Cannot pop from an empty array.");
                Value last = array->elements.back();
                array->elements.pop_back();
                return last;
            });
    }
    if (m == "length") {
        return std::make_shared<NativeFunction>("length", 0,
            [array](Interpreter&, std::vector<Value>&) -> Value {
                return static_cast<double>(array->elements.size());
            });
    }
    if (m == "contains") {
        return std::make_shared<NativeFunction>("contains", 1,
            [array](Interpreter&, std::vector<Value>& args) -> Value {
                for (auto& el : array->elements) if (el == args[0]) return true;
                return false;
            });
    }
    if (m == "indexOf") {
        return std::make_shared<NativeFunction>("indexOf", 1,
            [array](Interpreter&, std::vector<Value>& args) -> Value {
                for (size_t i = 0; i < array->elements.size(); i++) {
                    if (array->elements[i] == args[0]) return static_cast<double>(i);
                }
                return -1.0;
            });
    }
    if (m == "sort") {
        return std::make_shared<NativeFunction>("sort", 0,
            [array](Interpreter&, std::vector<Value>&) -> Value {
                std::sort(array->elements.begin(), array->elements.end(),
                          [](const Value& a, const Value& b) { return ascending(a, b, "sort"); });
                return array;
            });
    }
    if (m == "reverse") {
        return std::make_shared<NativeFunction>("reverse", 0,
            [array](Interpreter&, std::vector<Value>&) -> Value {
                std::reverse(array->elements.begin(), array->elements.end());
                return array;
            });
    }
    if (m == "slice") {
        return std::make_shared<NativeFunction>("slice", 2,
            [array](Interpreter&, std::vector<Value>& args) -> Value {
                if (!std::holds_alternative<double>(args[0]) || !std::holds_alternative<double>(args[1])) {
                    throw nativeError("slice", "start/end must be numbers.");
                }
                int start = static_cast<int>(std::get<double>(args[0]));
                int end = static_cast<int>(std::get<double>(args[1]));
                int len = static_cast<int>(array->elements.size());
                if (start < 0 || end > len || start > end) throw nativeError("slice", "Index out of range.");
                auto result = std::make_shared<ArrayObject>();
                result->elements.assign(array->elements.begin() + start, array->elements.begin() + end);
                return result;
            });
    }
    if (m == "binarySearch") {
        return std::make_shared<NativeFunction>("binarySearch", 1,
            [array](Interpreter&, std::vector<Value>& args) -> Value {
                int lo = 0, hi = static_cast<int>(array->elements.size()) - 1;
                while (lo <= hi) {
                    int mid = lo + (hi - lo) / 2;
                    if (array->elements[mid] == args[0]) return static_cast<double>(mid);
                    if (ascending(array->elements[mid], args[0], "binarySearch")) lo = mid + 1;
                    else hi = mid - 1;
                }
                return -1.0;
            });
    }

    throw RuntimeError(name, "Undefined array method '" + m + "'.");
}
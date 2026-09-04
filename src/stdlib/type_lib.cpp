// type_lib.cpp: each predicate just checks one Value variant alternative.
// isFunction() covers BOTH user functions and classes

#include "type_lib.h"
#include "native_function.h"
#include "../interpreter/array_object.h"

void registerTypeLib(std::shared_ptr<Environment> globals) {
    globals->define("isNumber", Value{std::make_shared<NativeFunction>(
        "isNumber", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<double>(args[0]);
        }
    )});

    globals->define("isString", Value{std::make_shared<NativeFunction>(
        "isString", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<std::string>(args[0]);
        }
    )});

    globals->define("isBool", Value{std::make_shared<NativeFunction>(
        "isBool", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<bool>(args[0]);
        }
    )});

    globals->define("isArray", Value{std::make_shared<NativeFunction>(
        "isArray", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<std::shared_ptr<ArrayObject>>(args[0]);
        }
    )});

    globals->define("isFunction", Value{std::make_shared<NativeFunction>(
        "isFunction", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<std::shared_ptr<Callable>>(args[0]);
        }
    )});

    globals->define("isNil", Value{std::make_shared<NativeFunction>(
        "isNil", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<std::monostate>(args[0]);
        }
    )});
}
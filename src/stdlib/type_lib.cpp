// type_lib.cpp: each predicate just checks one Value variant alternative.
// isFunction() covers BOTH user functions and classes

#include "type_lib.h"
#include "native_function.h"
#include "../interpreter/array_object.h"
#include "../interpreter/map_object.h"

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

    globals->define("isMap", Value{std::make_shared<NativeFunction>(
        "isMap", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<std::shared_ptr<MapObject>>(args[0]);
        }
    )});

    globals->define("isFunction", Value{std::make_shared<NativeFunction>(
        "isFunction", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<std::shared_ptr<Callable>>(args[0]);
        }
    )});

    // type(x): the type's name as a string. Functions, native functions, classes and bound methods are all "function" (the same set isFunction() accepts).
    globals->define("type", Value{std::make_shared<NativeFunction>(
        "type", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const Value& v = args[0];
            if (std::holds_alternative<std::monostate>(v)) return std::string("nil");
            if (std::holds_alternative<double>(v)) return std::string("number");
            if (std::holds_alternative<bool>(v)) return std::string("bool");
            if (std::holds_alternative<std::string>(v)) return std::string("string");
            if (std::holds_alternative<std::shared_ptr<Callable>>(v)) return std::string("function");
            if (std::holds_alternative<std::shared_ptr<ArrayObject>>(v)) return std::string("array");
            if (std::holds_alternative<std::shared_ptr<MapObject>>(v)) return std::string("map");
            if (std::holds_alternative<std::shared_ptr<LoxInstance>>(v)) return std::string("instance");
            return std::string("module");
        }
    )});

    globals->define("isNil", Value{std::make_shared<NativeFunction>(
        "isNil", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return std::holds_alternative<std::monostate>(args[0]);
        }
    )});
}
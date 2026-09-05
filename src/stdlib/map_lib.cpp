// map_lib.cpp: keys()/values() return NEW arrays (snapshots) 

#include "map_lib.h"
#include "native_function.h"
#include "../interpreter/map_object.h"
#include "../interpreter/array_object.h"

static std::shared_ptr<MapObject> requireMap(const std::string& fnName, const Value& v) {
    if (!std::holds_alternative<std::shared_ptr<MapObject>>(v)) {
        throw nativeError(fnName, "Expected a map argument.");
    }
    return std::get<std::shared_ptr<MapObject>>(v);
}

void registerMapLib(std::shared_ptr<Environment> globals) {
    // keys(map): array of all keys, in sorted order 
    // (MapObject uses std::map internally, so this is always alphabetical, not insertion order)
    globals->define("keys", Value{std::make_shared<NativeFunction>(
        "keys", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto map = requireMap("keys", args[0]);
            auto result = std::make_shared<ArrayObject>();
            for (auto& [key, value] : map->entries) {
                result->elements.push_back(key);
            }
            return result;
        }
    )});

    // values(map): array of all values, in the same (key-sorted) order as keys()
    globals->define("values", Value{std::make_shared<NativeFunction>(
        "values", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto map = requireMap("values", args[0]);
            auto result = std::make_shared<ArrayObject>();
            for (auto& [key, value] : map->entries) {
                result->elements.push_back(value);
            }
            return result;
        }
    )});

    // hasKey(map, key): true if the key exists in the map
    globals->define("hasKey", Value{std::make_shared<NativeFunction>(
        "hasKey", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto map = requireMap("hasKey", args[0]);
            if (!std::holds_alternative<std::string>(args[1])) {
                throw nativeError("hasKey", "Key must be a string.");
            }
            return map->entries.count(std::get<std::string>(args[1])) > 0;
        }
    )});

    // remove(map, key): deletes the entry in place; returns true if it existed
    globals->define("remove", Value{std::make_shared<NativeFunction>(
        "remove", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            auto map = requireMap("remove", args[0]);
            if (!std::holds_alternative<std::string>(args[1])) {
                throw nativeError("remove", "Key must be a string.");
            }
            return map->entries.erase(std::get<std::string>(args[1])) > 0;
        }
    )});
}
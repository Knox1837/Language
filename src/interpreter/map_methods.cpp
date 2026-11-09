// map_methods.cpp: mirrors array_methods.cpp's pattern for maps.

#include "map_methods.h"
#include "map_object.h"
#include "array_object.h"
#include "environment.h" // for RuntimeError
#include "../stdlib/native_function.h"

Value getMapMethod(std::shared_ptr<MapObject> map, const Token& name) {
    const std::string& m = name.lexeme;

    if (m == "keys") {
        return std::make_shared<NativeFunction>("keys", 0,
            [map](Interpreter&, std::vector<Value>&) -> Value {
                auto result = std::make_shared<ArrayObject>();
                for (auto& [key, value] : map->entries) result->elements.push_back(keyToValue<Value>(key));
                return result;
            });
    }
    if (m == "values") {
        return std::make_shared<NativeFunction>("values", 0,
            [map](Interpreter&, std::vector<Value>&) -> Value {
                auto result = std::make_shared<ArrayObject>();
                for (auto& [key, value] : map->entries) result->elements.push_back(value);
                return result;
            });
    }
    if (m == "hasKey") {
        return std::make_shared<NativeFunction>("hasKey", 1,
            [map](Interpreter&, std::vector<Value>& args) -> Value {
                KeyCheck check = checkKey(args[0]);
                if (check != KeyCheck::OK) throw nativeError("hasKey", keyProblem(check));
                return hasKey(map->entries, args[0]);
            });
    }
    if (m == "remove") {
        return std::make_shared<NativeFunction>("remove", 1,
            [map](Interpreter&, std::vector<Value>& args) -> Value {
                KeyCheck check = checkKey(args[0]);
                if (check != KeyCheck::OK) throw nativeError("remove", keyProblem(check));
                return eraseKey(map->entries, args[0]);
            });
    }
    if (m == "length") {
        return std::make_shared<NativeFunction>("length", 0,
            [map](Interpreter&, std::vector<Value>&) -> Value {
                return static_cast<double>(map->entries.size());
            });
    }

    throw RuntimeError(name, "Undefined map method '" + m + "'.");
}
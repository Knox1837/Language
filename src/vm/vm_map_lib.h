// vm_map_lib.h: the map operations, offered as free functions (keys(m)) and as bound methods (m.keys()) from one shared implementation.
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "vm_value.h"

class VMMap;

// Defines keys, values, hasKey and remove as global functions. (length(m) is defined with the array functions, since one function handles both.) Called from registerVMStdlib().
void registerVMMapLib(std::unordered_map<std::string, VMValue>& globals);

// Looks up the map method `name` (keys, values, hasKey, remove, length) and, if it exists, stores in `result` a native function already bound to `map`. Returns false if there is no such method.
bool getVMMapMethod(const std::shared_ptr<VMMap>& map, const std::string& name, VMValue& result);
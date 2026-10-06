// vm_array_lib.h: the array operations, offered as free functions (push(arr, x)) and as bound methods (arr.push(x)) from one shared implementation.
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "vm_value.h"

class VMArray;

// Defines push, pop, length, contains, indexOf, sort, reverse, slice and binarySearch as global functions. Called from registerVMStdlib().
void registerVMArrayLib(std::unordered_map<std::string, VMValue>& globals);

// Looks up the array method `name` (e.g. "push") and, if it exists, stores in `result` a native function already bound to `array` -- what `arr.push` evaluates to. Returns false if there is no such method.
bool getVMArrayMethod(const std::shared_ptr<VMArray>& array, const std::string& name, VMValue& result);
// vm_stdlib.h: the VM's native standard library, plus the value-to-string formatter shared by print and str().
#pragma once
#include <string>
#include <unordered_map>
#include "vm_value.h"

// Formats a value exactly like the tree-walker's stringifyValue(): whole numbers without a decimal part ("10"), other numbers via std::to_string ("3.140000").
std::string stringifyVMValue(const VMValue& value);

// Defines every native function as a global. Called once when the VM is constructed.
void registerVMStdlib(std::unordered_map<std::string, VMValue>& globals);
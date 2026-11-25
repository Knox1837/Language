// vm_stdlib.h: the VM's native standard library, plus the value-to-string formatter shared by print and str().
#pragma once
#include <string>
#include <unordered_map>
#include "vm_value.h"

// Formats a value exactly like the tree-walker's stringifyValue(): numbers through formatNumber() in util/number_format.h (the shortest text that reads back the same: "10", "3.14").
std::string stringifyVMValue(const VMValue& value);

// Defines every native function as a global. Called once when the VM is constructed.
void registerVMStdlib(std::unordered_map<std::string, VMValue>& globals);
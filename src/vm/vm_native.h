// vm_native.h: a native function callable from VM scripts. Vm counterpart of the tree-walker's NativeFunction (src/stdlib/native_function.h).
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "vm_value.h"

class VMNative {
public:
    // Reports failure by returning false and filling `error` (the VM turns that into a runtime error with the right line number).
    // On success, writes the return value into `result` and returns true. `args` holds exactly `arity` values (the VM checks the count first).
    using Fn = std::function<bool(std::vector<VMValue>& args, VMValue& result, std::string& error)>;

    std::string name;
    int arity = 0;
    Fn fn;

    VMNative(std::string name, int arity, Fn fn) : name(std::move(name)), arity(arity), fn(std::move(fn)) {}
};
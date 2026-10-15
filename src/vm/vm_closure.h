// vm_closure.h: the actual runtime callable value. 
// A VMFunction alone is just compiled code with no captured state
#pragma once

#include <vector>
#include <memory>
#include "vm_function.h"
#include "vm_upvalue.h"

class VMModule; // vm_module.h

class VMClosure {
public:
    std::shared_ptr<VMFunction> function;
    std::vector<std::shared_ptr<VMUpvalue>> upvalues;

    // The module (global namespace) this closure was created in. Global-variable instructions look names up here, NOT in "whatever module is running", so a function imported from another file keeps seeing its own file's globals when called from anywhere.
    VMModule* module = nullptr;
};
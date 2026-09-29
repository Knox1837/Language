// vm_closure.h: the actual runtime callable value. 
// A VMFunction alone is just compiled code with no captured state
#pragma once

#include <vector>
#include <memory>
#include "vm_function.h"
#include "vm_upvalue.h"

class VMClosure {
public:
    std::shared_ptr<VMFunction> function;
    std::vector<std::shared_ptr<VMUpvalue>> upvalues;
};
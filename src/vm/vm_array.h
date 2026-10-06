// vm_array.h: the runtime representation of an array 
#pragma once
#include <vector>
#include "vm_value.h"

class VMArray {
public:
    std::vector<VMValue> elements;
};
// vm_map.h: the runtime representation of a map
#pragma once
#include <map>
#include <string>
#include "vm_value.h"

class VMMap {
public:
    std::map<std::string, VMValue> entries;
};
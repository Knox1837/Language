// array_object.h: the runtime representation of an array. 
// Kept in itsown header (not value.h) to avoid a circular include
#pragma once

#include <vector>
#include "value.h"

class ArrayObject {
public:
    std::vector<Value> elements;
};
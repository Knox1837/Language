// vm_array.h: the runtime representation of an array 
#pragma once
#include <vector>
#include "gc.h"
#include "vm_value.h"

class VMArray : public GCObject {
public:
    std::vector<VMValue> elements;

    void traceChildren(const GCVisitor& visit) const override {
        for (const auto& element : elements) gcVisitValue(element, visit);
    }
    void dropChildren() override { elements.clear(); }
};
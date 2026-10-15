// vm_map.h: the runtime representation of a map
#pragma once
#include <map>
#include <string>
#include "gc.h"
#include "vm_value.h"

class VMMap : public GCObject {
public:
    std::map<std::string, VMValue> entries;

    void traceChildren(const GCVisitor& visit) const override {
        for (const auto& entry : entries) gcVisitValue(entry.second, visit);
    }
    void dropChildren() override { entries.clear(); }
};
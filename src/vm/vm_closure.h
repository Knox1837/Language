// vm_closure.h: the actual runtime callable value. 
// A VMFunction alone is just compiled code with no captured state
#pragma once

#include <vector>
#include <memory>
#include "gc.h"
#include "vm_function.h"
#include "vm_upvalue.h"

class VMModule; // vm_module.h

class VMClosure : public GCObject {
public:
    std::shared_ptr<VMFunction> function;
    std::vector<std::shared_ptr<VMUpvalue>> upvalues;

    // The module (global namespace) this closure was created in. Global-variable instructions look names up here, NOT in "whatever module is running", so a function imported from another file keeps seeing its own file's globals when called from anywhere.
    // A raw pointer on purpose: every module lives as long as the VM (the VM owns the main one, and its module cache owns the rest), and a shared_ptr would make a cycle (module -> globals -> closure -> module) that never frees.
    VMModule* module = nullptr;

    void traceChildren(const GCVisitor& visit) const override {
        for (const auto& upvalue : upvalues) visit(upvalue.get()); // `function` is not a GCObject, and `module` is a raw pointer
    }
    void dropChildren() override { upvalues.clear(); }
};
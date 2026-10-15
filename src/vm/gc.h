// gc.h: garbage collector cycle collection for the VM's heap objects.
#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include "vm_value.h"

class GCObject;
using GCVisitor = std::function<void(GCObject*)>;

// Calls `visit` for the heap object held by `value`, if it holds one (an array, map, closure, ...). Numbers, strings and compiled functions can't be part of a cycle and are ignored.
void gcVisitValue(const VMValue& value, const GCVisitor& visit);

class GCObject : public std::enable_shared_from_this<GCObject> {
public:
    GCObject();
    virtual ~GCObject();

    // Objects live in an intrusive list, so copying one would have to mean a second list node. Nothing copies them; deleting the copy operations keeps it that way.
    GCObject(const GCObject&) = delete;
    GCObject& operator=(const GCObject&) = delete;

    // Calls `visit` once for every shared_ptr this object holds that can point at a GCObject (directly, or inside a VMValue). Never raw pointers or weak pointers: those add nothing to a use count.
    virtual void traceChildren(const GCVisitor& visit) const = 0;

    // Releases everything this object references, so that the cycle it was part of falls apart. Only ever called on objects the collector has proven unreachable.
    virtual void dropChildren() = 0;

private:
    friend struct GC;
    GCObject* prevObject = nullptr;
    GCObject* nextObject = nullptr;
    long externalRefs = 0; // scratch for collect(): use count minus references from other heap objects
    bool reachable = false; // scratch for collect()
};

// State behind GC::shouldCollect(), public only so that the check can be inlined: it runs on every call and every loop iteration, so it has to cost no more than one comparison.
extern size_t gcAllocatedSinceCollect;
extern size_t gcThreshold;

struct GC {
    // Finds and frees every unreachable cycle. Returns how many heap objects were freed as garbage. Safe to call at any moment (see above), including from inside a native function.
    static size_t collect();

    // True once enough objects have been allocated since the last collection that the VM should run one. The threshold grows with the live heap, so the total cost stays proportional to the allocation rate.
    static bool shouldCollect() { return gcAllocatedSinceCollect >= gcThreshold; }

    // Number of heap objects that currently exist (live or not yet collected).
    static size_t liveCount();
};
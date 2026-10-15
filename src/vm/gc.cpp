// gc.cpp: see gc.h for the idea. This file is the list of all heap objects and the collector itself.
#include "gc.h"
#include <algorithm>
#include <vector>
#include "vm_array.h"
#include "vm_class.h"
#include "vm_closure.h"
#include "vm_map.h"
#include "vm_module.h"
#include "vm_native.h"
#include "vm_upvalue.h"

namespace {
// Never collect more often than every this many allocations, however small the heap is.
constexpr size_t MIN_THRESHOLD = 10000;

GCObject* allObjects = nullptr; // head of the intrusive list of every GCObject
size_t liveObjects = 0;
bool collecting = false;
} // namespace

size_t gcAllocatedSinceCollect = 0;
#ifdef MYLANG_GC_STRESS
size_t gcThreshold = 1; // test builds: ask for a collection at every opportunity, to flush out any use-after-free
#else
size_t gcThreshold = MIN_THRESHOLD;
#endif

GCObject::GCObject() {
    nextObject = allObjects;
    if (allObjects) allObjects->prevObject = this;
    allObjects = this;
    ++liveObjects;
    ++gcAllocatedSinceCollect;
}

GCObject::~GCObject() {
    if (prevObject) prevObject->nextObject = nextObject;
    else allObjects = nextObject;
    if (nextObject) nextObject->prevObject = prevObject;
    --liveObjects;
}

void gcVisitValue(const VMValue& value, const GCVisitor& visit) {
    // One branch per alternative of VMValue that is a GCObject. (VMFunction is not: a compiled function holds only constants, never a closure or container.)
    if (auto p = std::get_if<std::shared_ptr<VMClosure>>(&value)) { if (*p) visit(p->get()); }
    else if (auto p = std::get_if<std::shared_ptr<VMNative>>(&value)) { if (*p) visit(p->get()); }
    else if (auto p = std::get_if<std::shared_ptr<VMArray>>(&value)) { if (*p) visit(p->get()); }
    else if (auto p = std::get_if<std::shared_ptr<VMMap>>(&value)) { if (*p) visit(p->get()); }
    else if (auto p = std::get_if<std::shared_ptr<VMClass>>(&value)) { if (*p) visit(p->get()); }
    else if (auto p = std::get_if<std::shared_ptr<VMInstance>>(&value)) { if (*p) visit(p->get()); }
    else if (auto p = std::get_if<std::shared_ptr<VMBoundMethod>>(&value)) { if (*p) visit(p->get()); }
    else if (auto p = std::get_if<std::shared_ptr<VMModule>>(&value)) { if (*p) visit(p->get()); }
}

size_t GC::liveCount() { return liveObjects; }

size_t GC::collect() {
    if (collecting) return 0;
    collecting = true;
    gcAllocatedSinceCollect = 0;

    // Snapshot the list first: freeing objects below edits the list, but the snapshot is only used before that.
    std::vector<GCObject*> objects;
    objects.reserve(liveObjects);
    for (GCObject* o = allObjects; o; o = o->nextObject) objects.push_back(o);

    // Step 1: start every object at its shared_ptr use count. weak_ptr::use_count() reads the count without creating a new reference.
    bool consistent = true;
    for (GCObject* o : objects) {
        long uses = static_cast<long>(o->weak_from_this().use_count());
        if (uses == 0) consistent = false; // not owned by a shared_ptr: every object is made with make_shared, so this can't happen, but if it did the arithmetic below would be meaningless
        o->externalRefs = uses;
        o->reachable = false;
    }

    // Step 2: subtract the references that come from other heap objects.
    if (consistent) {
        for (GCObject* o : objects) o->traceChildren([](GCObject* child) { --child->externalRefs; });
        // A negative count means traceChildren() reported a reference that does not exist. Free nothing rather than risk freeing something live.
        for (GCObject* o : objects) if (o->externalRefs < 0) consistent = false;
    }

    size_t freed = 0;
    if (consistent) {
        // Step 3: anything still referenced from outside is alive, and so is everything it reaches.
        std::vector<GCObject*> pending;
        for (GCObject* o : objects) {
            if (o->externalRefs > 0) {
                o->reachable = true;
                pending.push_back(o);
            }
        }
        while (!pending.empty()) {
            GCObject* o = pending.back();
            pending.pop_back();
            o->traceChildren([&pending](GCObject* child) {
                if (!child->reachable) {
                    child->reachable = true;
                    pending.push_back(child);
                }
            });
        }

        // Step 4: the rest is garbage. Take a strong reference to each first, so that clearing one object's contents can't free another one that is still waiting its turn; they are all released together at the end.
        std::vector<std::shared_ptr<GCObject>> garbage;
        for (GCObject* o : objects) {
            if (!o->reachable) {
                if (auto strong = o->weak_from_this().lock()) garbage.push_back(std::move(strong));
            }
        }
        freed = garbage.size();
        for (auto& g : garbage) g->dropChildren();
        garbage.clear();
    }

#ifndef MYLANG_GC_STRESS
    gcThreshold = std::max(MIN_THRESHOLD, liveObjects); // wait for as many new objects as survived, so work per allocation stays constant
#endif
    collecting = false;
    return freed;
}
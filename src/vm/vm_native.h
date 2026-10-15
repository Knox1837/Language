// vm_native.h: a native function callable from VM scripts. Vm counterpart of the tree-walker's NativeFunction (src/stdlib/native_function.h).
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "gc.h"
#include "vm_value.h"

class VMNative : public GCObject {
public:
    // Reports failure by returning false and filling `error` (the VM turns that into a runtime error with the right line number).
    // On success, writes the return value into `result` and returns true. `args` holds exactly `arity` values (the VM checks the count first).
    using Fn = std::function<bool(std::vector<VMValue>& args, VMValue& result, std::string& error)>;

    std::string name;
    int arity = 0;
    Fn fn;

    // For a native bound to one array or map (`a.push`): that array or map. It is kept HERE, not captured inside `fn`, so the collector can see the reference: `a[0] = a.push;` makes a cycle (a -> native -> a) that it must be able to find.
    // `fn` reaches the receiver through a raw pointer and shared_from_this(), which is safe because this field keeps it alive for as long as the native exists.
    VMValue boundReceiver;

    VMNative(std::string name, int arity, Fn fn) : name(std::move(name)), arity(arity), fn(std::move(fn)) {}

    void traceChildren(const GCVisitor& visit) const override { gcVisitValue(boundReceiver, visit); }
    void dropChildren() override { boundReceiver = VMValue{}; }
};
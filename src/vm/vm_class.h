// vm_class.h: the runtime objects behind classes 
// VM-side counterparts of the tree-walker's LoxClass, LoxInstance and (bound) UserFunction. Kept together because they only make sense as a set: a class makes instances, an instance finds methods on its class, and reading a method off an instance yields a bound method.
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "vm_closure.h"
#include "vm_value.h"

class VMClass {
public:
    std::string name;
    std::shared_ptr<VMClass> superclass; // nullptr when declared without "< Superclass"
    std::unordered_map<std::string, std::shared_ptr<VMClosure>> methods;

    explicit VMClass(std::string name) : name(std::move(name)) {}

    // Checks this class first, then walks up the superclass chain, exactly like the tree-walker's LoxClass::findMethod.
    // (Methods are fixed once the class declaration has run, so looking them up through the chain is equivalent to copying them down at inheritance time.)
    std::shared_ptr<VMClosure> findMethod(const std::string& methodName) const {
        for (const VMClass* klass = this; klass != nullptr; klass = klass->superclass.get()) {
            auto found = klass->methods.find(methodName);
            if (found != klass->methods.end()) return found->second;
        }
        return nullptr;
    }
};

class VMInstance {
public:
    std::shared_ptr<VMClass> klass;
    std::unordered_map<std::string, VMValue> fields; // created on first assignment; there is no declared field list

    explicit VMInstance(std::shared_ptr<VMClass> klass) : klass(std::move(klass)) {}
};

// A method read off an instance (`obj.method`, `super.method`): the method's closure plus the instance it was read from, so calling it later still has the right `this`.
class VMBoundMethod {
public:
    VMValue receiver;
    std::shared_ptr<VMClosure> method;

    VMBoundMethod(VMValue receiver, std::shared_ptr<VMClosure> method) : receiver(std::move(receiver)), method(std::move(method)) {}
};
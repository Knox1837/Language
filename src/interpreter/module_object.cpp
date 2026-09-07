// module_object.cpp: property access just delegates to the wrapped Environment's own get()

#include "module_object.h"

ModuleObject::ModuleObject(std::shared_ptr<Environment> moduleEnvironment)
    : moduleEnvironment(std::move(moduleEnvironment)) {}

Value ModuleObject::get(const Token& name) {
    return moduleEnvironment->get(name);
}
// vm_module.h: the VM-side counterpart of the tree-walker's ModuleObject.
// A module is one file's own global namespace: the main script has one, and every `import` creates another.
// Each module gets its own table of globals (its own copy of the standard library plus whatever the file defines at top level), which is what keeps an imported file isolated from the importer.
#pragma once
#include <string>
#include <unordered_map>
#include "vm_value.h"

class VMModule {
public:
    std::string path; // the resolved file path (the cache key); empty for the main script's module
    std::unordered_map<std::string, VMValue> globals;
};
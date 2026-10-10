// vm_map_lib.cpp: map operations, written ONCE and exposed two ways (free functions and bound methods), mirroring vm_array_lib.cpp.
// Names, arities and error messages match the tree-walker's map_lib.cpp and map_methods.cpp.
#include "vm_map_lib.h"
#include "vm_array.h"
#include "vm_map.h"
#include "vm_native.h"

namespace {

using MapPtr = std::shared_ptr<VMMap>;

// `rest` holds the arguments AFTER the map, so keys(m) and m.keys() both see {}, and hasKey(m, k) and m.hasKey(k) both see {k}.
using MapOp = bool (*)(const MapPtr& map, std::vector<VMValue>& rest, VMValue& result, std::string& error);

struct MapOpInfo {
    const char* name;
    int arity; // counts `rest` only: the free-function form takes one more (the map itself)
    MapOp fn;
};

// keys()/values() return NEW arrays (snapshots), in key-sorted order; later changes to the map don't affect them.
bool opKeys(const MapPtr& map, std::vector<VMValue>&, VMValue& result, std::string&) {
    auto out = std::make_shared<VMArray>();
    for (const auto& entry : map->entries) out->elements.push_back(entry.first);
    result = out;
    return true;
}

bool opValues(const MapPtr& map, std::vector<VMValue>&, VMValue& result, std::string&) {
    auto out = std::make_shared<VMArray>();
    for (const auto& entry : map->entries) out->elements.push_back(entry.second);
    result = out;
    return true;
}

bool opHasKey(const MapPtr& map, std::vector<VMValue>& rest, VMValue& result, std::string& error) {
    if (!isVMString(rest[0])) {
        error = "Key must be a string.";
        return false;
    }
    result = map->entries.count(asVMString(rest[0])) > 0;
    return true;
}

// remove(key): deletes the entry in place; true if it existed
bool opRemove(const MapPtr& map, std::vector<VMValue>& rest, VMValue& result, std::string& error) {
    if (!isVMString(rest[0])) {
        error = "Key must be a string.";
        return false;
    }
    result = map->entries.erase(asVMString(rest[0])) > 0;
    return true;
}

bool opLength(const MapPtr& map, std::vector<VMValue>&, VMValue& result, std::string&) {
    result = static_cast<double>(map->entries.size());
    return true;
}

const MapOpInfo kOps[] = {
    {"keys",    0, opKeys},
    {"values",  0, opValues},
    {"hasKey",  1, opHasKey},
    {"remove",  1, opRemove},
    {"length",  0, opLength},
};

} // namespace

void registerVMMapLib(std::unordered_map<std::string, VMValue>& globals) {
    for (const auto& op : kOps) {
        // `length` exists only as a METHOD here; the free function length() is shared with arrays and lives in vm_array_lib.cpp.
        if (std::string(op.name) == "length") continue;
        MapOp fn = op.fn;
        globals[op.name] = VMValue{std::make_shared<VMNative>(
            op.name, op.arity + 1,
            [fn](std::vector<VMValue>& args, VMValue& result, std::string& error) {
                if (!isVMMap(args[0])) {
                    error = "Expected a map argument.";
                    return false;
                }
                std::vector<VMValue> rest(args.begin() + 1, args.end());
                return fn(asVMMap(args[0]), rest, result, error);
            })};
    }
}

bool getVMMapMethod(const std::shared_ptr<VMMap>& map, const std::string& name, VMValue& result) {
    for (const auto& op : kOps) {
        if (name == op.name) {
            MapOp fn = op.fn;
            // Captures the map, so the returned native stays bound to this instance: `var k = m.keys; k()` lists m's keys at call time.
            result = VMValue{std::make_shared<VMNative>(
                op.name, op.arity,
                [map, fn](std::vector<VMValue>& args, VMValue& out, std::string& error) {
                    return fn(map, args, out, error);
                })};
            return true;
        }
    }
    return false;
}
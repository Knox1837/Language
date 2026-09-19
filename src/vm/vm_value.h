// vm_value.h: the VM's own value type, separate from the tree-walking interpreter's Value (src/interpreter/value.h).
#pragma once
#include <variant>
#include <string>
#include <memory>

class VMFunction; // vm_function.h: forward declared to break a circular include: 
// VMFunction needs a Chunk, and Chunk's constant pool needs to hold VMValue, so VMFunction can't be fully defined here without a cycle.

using VMValue = std::variant<std::monostate, double, bool, std::string, std::shared_ptr<VMFunction>>;

inline bool isVMNumber(const VMValue& v) { return std::holds_alternative<double>(v); }
inline bool isVMBool(const VMValue& v) { return std::holds_alternative<bool>(v); }
inline bool isVMString(const VMValue& v) { return std::holds_alternative<std::string>(v); }
inline bool isVMNil(const VMValue& v) { return std::holds_alternative<std::monostate>(v); }
inline bool isVMFunction(const VMValue& v) { return std::holds_alternative<std::shared_ptr<VMFunction>>(v); }

inline double asVMNumber(const VMValue& v) { return std::get<double>(v); }
inline bool asVMBool(const VMValue& v) { return std::get<bool>(v); }
inline const std::string& asVMString(const VMValue& v) { return std::get<std::string>(v); }
inline std::shared_ptr<VMFunction> asVMFunction(const VMValue& v) { return std::get<std::shared_ptr<VMFunction>>(v); }

// Truthiness, matching the tree-walker's rule exactly: only nil and false are falsey; everything else (including 0 and "") is truthy.
inline bool isVMTruthy(const VMValue& v) {
    if (isVMNil(v)) return false;
    if (isVMBool(v)) return asVMBool(v);
    return true;
}
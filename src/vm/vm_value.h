// vm_value.h: the VM's own value type, separate from the tree-walking interpreter's Value (src/interpreter/value.h).
#pragma once
#include <variant>
#include <string>

using VMValue = std::variant<std::monostate, double, bool, std::string>;

inline bool isVMNumber(const VMValue& v) { return std::holds_alternative<double>(v); }
inline bool isVMBool(const VMValue& v) { return std::holds_alternative<bool>(v); }
inline bool isVMString(const VMValue& v) { return std::holds_alternative<std::string>(v); }
inline bool isVMNil(const VMValue& v) { return std::holds_alternative<std::monostate>(v); }

inline double asVMNumber(const VMValue& v) { return std::get<double>(v); }
inline bool asVMBool(const VMValue& v) { return std::get<bool>(v); }
inline const std::string& asVMString(const VMValue& v) { return std::get<std::string>(v); }

// Truthiness, matching the tree-walker's rule exactly: only nil and false are falsey; everything else (including 0 and "") is truthy.
inline bool isVMTruthy(const VMValue& v) {
    if (isVMNil(v)) return false;
    if (isVMBool(v)) return asVMBool(v);
    return true;
}
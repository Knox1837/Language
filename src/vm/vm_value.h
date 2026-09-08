// vm_value.h: the VM's own value type, separate from the tree-walking interpreter's Value (src/interpreter/value.h).
#pragma once
#include <variant>
#include <string>

using VMValue = std::variant<std::monostate, double, std::string>;

inline bool isVMNumber(const VMValue& v) { return std::holds_alternative<double>(v); }
inline bool isVMString(const VMValue& v) { return std::holds_alternative<std::string>(v); }
inline bool isVMNil(const VMValue& v) { return std::holds_alternative<std::monostate>(v); }

inline double asVMNumber(const VMValue& v) { return std::get<double>(v); }
inline const std::string& asVMString(const VMValue& v) { return std::get<std::string>(v); }
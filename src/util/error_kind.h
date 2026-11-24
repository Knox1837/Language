// error_kind.h: the "kind" of a runtime error, as a string, for the `kind` entry of the map a two-variable catch receives. Both engines use this one function, so a message gets the same kind in either.
// A language error carries only its message, and the messages are the single place that says what went wrong, so the kind is worked out from the START of the message (never from anywhere inside it, since a message can contain user text, such as the name in "Undefined variable 'x'."). An error raised with error(value) or assert is not classified at all: its kind is always "user", whatever the value says.
//To give a new error message a kind, add its beginning to the table below. A message the table doesn't know is "other"; test_error_kinds triggers one error of each kind.
#pragma once
#include <cctype>
#include <string>

inline bool errorStartsWith(const std::string& message, const char* prefix) {
    return message.rfind(prefix, 0) == 0;
}

inline std::string classifyError(const std::string& message) {
    // "Expected 1 arguments but got 2." / "Expected 1 or 2 arguments ...": a wrong NUMBER of arguments. ("Expected a string argument." is about the type of one.)
    if (errorStartsWith(message, "Expected ") && message.size() > 9 && std::isdigit(static_cast<unsigned char>(message[9]))) return "arity";

    struct Rule { const char* prefix; const char* kind; };
    static const Rule rules[] = {
        // a value of the wrong type
        {"Operands must be", "type"}, {"Operand must be", "type"}, {"Expected a", "type"}, {"Can only call", "type"},
        {"Can only iterate", "type"}, {"Can only sort", "type"}, {"Only ", "type"}, {"Array index must be", "type"},
        {"String index must be", "type"}, {"Index must be", "type"}, {"Comparator must return", "type"},
        {"Map keys must be", "type"}, {"First argument must be", "type"}, {"start/end must be", "type"},
        {"Superclass must be", "type"}, {"Strings are immutable", "type"},
        // the right type, but a value the operation can't accept
        {"Division by zero", "value"}, {"Modulo by zero", "value"}, {"Cannot take the square root", "value"},
        {"Argument must be positive", "value"}, {"min must not be greater", "value"}, {"delimiter must not be empty", "value"},
        {"old string must not be empty", "value"}, {"Invalid number:", "value"}, {"Number out of range:", "value"},
        {"Map key can't be NaN", "value"},
        // a position that does not exist
        {"Array index out of range", "range"}, {"Index out of range", "range"}, {"String index out of range", "range"},
        {"Cannot pop from an empty array", "range"}, {"Exit code is out of range", "range"},
        // a map key that is not there (before the general "Undefined " rule)
        {"Undefined map key", "key"},
        // a name that is not defined
        {"Undefined ", "name"},
        // files
        {"Could not open file", "io"}, {"Could not delete", "io"}, {"Could not read file", "io"}, {"Could not write to file", "io"},
        {"Could not list folder", "io"}, {"Could not create folder", "io"}, {"Could not remove folder", "io"}, {"Could not rename", "io"}, {"Could not copy", "io"},
        // imports
        {"Could not open imported file", "import"}, {"Could not compile imported file", "import"},
        {"Circular import", "import"}, {"Imports are not available", "import"},
    };
    for (const Rule& rule : rules) {
        if (errorStartsWith(message, rule.prefix)) return rule.kind;
    }
    return "other";
}
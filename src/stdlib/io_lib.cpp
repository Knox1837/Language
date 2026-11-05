// io_lib.cpp: input() reads a full line from stdin; readFile, writeFile, appendFile, readLines, fileExists and deleteFile work on files (the work itself is in util/file_io.h, shared with the VM). 
// Returns "" (not an error) on EOF/stream failure, so scripts reading input in a loop can just check for an empty string rather than handling an exception.

#include "io_lib.h"
#include "native_function.h"
#include "../interpreter/array_object.h"
#include "../util/file_io.h"
#include <iostream>

static const std::string& requireString(const std::string& fnName, const Value& v) {
    if (!std::holds_alternative<std::string>(v)) {
        throw nativeError(fnName, "Expected a string argument.");
    }
    return std::get<std::string>(v);
}

void registerIoLib(std::shared_ptr<Environment> globals) {
    globals->define("input", Value{std::make_shared<NativeFunction>(
        "input", 0,
        [](Interpreter&, std::vector<Value>&) -> Value {
            std::string line;
            if (!std::getline(std::cin, line)) {
                return std::string(""); // EOF or read failure
            }
            return line;
        }
    )});

    // File functions: a relative path is relative to the folder the program was started from (not to the script). Failures are ordinary runtime errors, so a script can catch them with try/catch.
    globals->define("readFile", Value{std::make_shared<NativeFunction>(
        "readFile", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& path = requireString("readFile", args[0]);
            std::string text, error;
            if (!fileio::readAll(path, text, error)) throw nativeError("readFile", error);
            return text;
        }
    )});

    globals->define("writeFile", Value{std::make_shared<NativeFunction>(
        "writeFile", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& path = requireString("writeFile", args[0]);
            const std::string& text = requireString("writeFile", args[1]);
            std::string error;
            if (!fileio::writeAll(path, text, false, error)) throw nativeError("writeFile", error);
            return std::monostate{};
        }
    )});

    globals->define("appendFile", Value{std::make_shared<NativeFunction>(
        "appendFile", 2,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& path = requireString("appendFile", args[0]);
            const std::string& text = requireString("appendFile", args[1]);
            std::string error;
            if (!fileio::writeAll(path, text, true, error)) throw nativeError("appendFile", error);
            return std::monostate{};
        }
    )});

    globals->define("readLines", Value{std::make_shared<NativeFunction>(
        "readLines", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& path = requireString("readLines", args[0]);
            std::string text, error;
            if (!fileio::readAll(path, text, error)) throw nativeError("readLines", error);
            auto lines = std::make_shared<ArrayObject>();
            for (std::string& line : fileio::splitLines(text)) lines->elements.push_back(Value{std::move(line)});
            return Value{lines};
        }
    )});

    globals->define("fileExists", Value{std::make_shared<NativeFunction>(
        "fileExists", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            return fileio::exists(requireString("fileExists", args[0]));
        }
    )});

    globals->define("deleteFile", Value{std::make_shared<NativeFunction>(
        "deleteFile", 1,
        [](Interpreter&, std::vector<Value>& args) -> Value {
            const std::string& path = requireString("deleteFile", args[0]);
            bool removed = false;
            std::string error;
            if (!fileio::removeFile(path, removed, error)) throw nativeError("deleteFile", error);
            return removed;
        }
    )});
}
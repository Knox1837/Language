// process.h: process-wide state shared by both engines: the script's command-line arguments (set once by main.cpp, read by the `args()` built-in) and a clean way for `exit()` to end the program.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// The arguments that came after the script path on the command line (the script path itself is not included).
inline std::vector<std::string>& scriptArgs() {
    static std::vector<std::string> args;
    return args;
}

// Ends the program immediately with `code`. Output is flushed first; the exit itself skips static destructors (_Exit), because `exit()` is called from deep inside a running interpreter whose own objects are still in use.
[[noreturn]] inline void exitProcess(int code) {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    std::_Exit(code);
}

// Converts the number given to `exit()` into an exit code. Returns false if it can't be one (NaN, infinity, or too big for an int).
inline bool toExitCode(double d, int& out) {
    if (!(d > -2147483648.0 && d < 2147483648.0)) return false; // also false for NaN
    out = static_cast<int>(d); // truncates toward zero
    return true;
}
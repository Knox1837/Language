// trace.h: what both engines share about a stack trace: the labels of the frames and how a list of them is turned into lines of text. A trace is a list of entries, INNERMOST first: the line the frame is currently executing and where it is ("inner()", "<fn>", "module helper.mylang", "script").
#pragma once
#include <string>
#include <vector>

struct TraceEntry {
    int line;
    std::string where;
};

// "name()" for a named function, "<fn>" for an anonymous one (def(x) { ... }).
inline std::string traceFunctionLabel(const std::string& name) {
    return name.empty() ? "<fn>" : name + "()";
}

// The top-level code of an imported file. Only the file's name is used, so a trace reads the same wherever the project lives.
inline std::string traceModuleLabel(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return "module " + (slash == std::string::npos ? path : path.substr(slash + 1));
}

// One text line per entry: "[line 5] in inner()". A trace longer than 21 entries (deep recursion) keeps its innermost 10 and outermost 10 and says how many it left out in between.
inline std::vector<std::string> formatTrace(const std::vector<TraceEntry>& entries) {
    const size_t keepEach = 10;
    auto text = [](const TraceEntry& e) { return "[line " + std::to_string(e.line) + "] in " + e.where; };
    std::vector<std::string> lines;
    if (entries.size() <= 2 * keepEach + 1) {
        for (const TraceEntry& e : entries) lines.push_back(text(e));
        return lines;
    }
    for (size_t i = 0; i < keepEach; i++) lines.push_back(text(entries[i]));
    lines.push_back("... " + std::to_string(entries.size() - 2 * keepEach) + " more frames ...");
    for (size_t i = entries.size() - keepEach; i < entries.size(); i++) lines.push_back(text(entries[i]));
    return lines;
}
// call_stack.h: the tree-walker's record of which user functions are running, for stack traces. One stack for the whole process, shared by the main script's interpreter and the interpreters that run imported files.
// Each frame says WHERE it is (its label) and the line of the CALL that made it (in the caller). A frame is popped when its function returns normally; when a runtime error passes through, the frames stay, so whoever finally handles the error (a `try`, or interpret() at the top level) can read the whole stack as it was when the error was raised, and then cuts the stack back.
#pragma once
#include <string>
#include <vector>

struct CallInfo {
    std::string where;
    int callLine;
};

inline std::vector<CallInfo>& callStack() {
    static std::vector<CallInfo> stack;
    return stack;
}

// The line of the call expression being dispatched right now: what a function entered through it records as its callLine (including a callback a built-in such as sort() calls, which has no call expression of its own).
inline int& currentCallLine() {
    static int line = 0;
    return line;
}

struct CallLineScope {
    int saved;
    explicit CallLineScope(int line) : saved(currentCallLine()) { currentCallLine() = line; }
    ~CallLineScope() { currentCallLine() = saved; }
};

class CallFrameScope {
public:
    explicit CallFrameScope(std::string where, int callLine = -1) : depth(callStack().size()) {
        callStack().push_back(CallInfo{std::move(where), callLine < 0 ? currentCallLine() : callLine});
    }
    void keep() { keepOnError = true; } // an error is passing through: leave the frame on the stack for the handler to read
    ~CallFrameScope() {
        if (!keepOnError && callStack().size() > depth) callStack().resize(depth);
    }
private:
    size_t depth;
    bool keepOnError = false;
};
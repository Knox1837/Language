// compiler.h: a single-pass Pratt parser that reads tokens (reusing the existing Lexer/Token from src/lexer/) and emits bytecode directly into a Chunk, with no separate AST step.
#pragma once
#include <vector>
#include <string>
#include <memory>
#include "../lexer/token.h"
#include "chunk.h"
#include "vm_function.h"

// One tracked local variable: its name (for resolving references to it) and the scope depth it was declared at (used to know which locals to discard when a block ends). 
struct LocalVar {
    Token name;
    int depth;
    bool isCaptured = false; // true once some NESTED function's closure captures this local as an upvalue — controls whether leaving its scope emits OP_CLOSE_UPVALUE (heap-preserve it) or a plain OP_POP (just discard it)
};

// Describes ONE upvalue a function captures, recorded at compile time.
struct UpvalueInfo {
    uint8_t index;
    bool isLocal;
};

// What kind of function body is being compiled. A METHOD or INITIALIZER has the receiver (`this`) in slot 0 instead of its own closure, and an INITIALIZER always returns it.
enum class FunctionType { SCRIPT, FUNCTION, METHOD, INITIALIZER };

// One loop that is currently being compiled, so `break` and `continue` inside its body know where to go.
struct LoopInfo {
    int scopeDepth;                    // scope depth of the loop itself; locals declared DEEPER than this belong to the body and must be discarded when a break/continue leaves it
    size_t continueTarget;             // where `continue` jumps back to: the condition for a while loop, the increment clause for a for loop that has one
    std::vector<size_t> breakJumps;    // forward jumps emitted by `break`, patched to land just after the loop
};

// Compile-time state for ONE function currently being compiled (the top-level script is FunctionState #0). 
// Kept on a stack in Compiler so entering a nested function declaration is just push-a-new-one, compile its body, pop back to the enclosing state.
struct FunctionState {
    std::shared_ptr<VMFunction> function;
    std::vector<LocalVar> locals;
    std::vector<UpvalueInfo> upvalues;
    int scopeDepth = 0;
    bool reportedConstantOverflow = false; // so a function that runs out of constants gets ONE error message, not one per extra constant
    FunctionType type = FunctionType::SCRIPT;
    std::vector<LoopInfo> loops = {}; // loops enclosing the current position in THIS function, innermost last (a loop outside a function does not make `break` legal inside it)
};

class Compiler {
public:
    // Compiles `source` into a top-level VMFunction (name "<script>", arity 0) and returns it, or nullptr on a syntax error (with messages already printed to stderr)  mirroring the tree-walker's parser reporting to stderr and continuing rather than throwing, so one bad line doesn't stop the whole compile.
    std::shared_ptr<VMFunction> compile(const std::string& source);

private:
    std::vector<Token> tokens;
    size_t current = 0;
    bool hadError = false;

    std::vector<FunctionState> functionStack;

    // One entry per class declaration currently being compiled (innermost last); the flag says whether that class has a superclass.
    std::vector<bool> classStack;
    FunctionState& current_() { return functionStack.back(); } // the function currently being compiled into
    Chunk& currentChunk() { return current_().function->chunk; }

    // Precedence levels, lowest to highest. ASSIGNMENT sits below TERM
    // so that e.g. parsing the right-hand side of "x = 1 + 2" correctly consumes the whole "1 + 2" rather than stopping after "1".
    enum class Precedence {
        NONE,
        ASSIGNMENT, // =
        OR,         // or
        AND,        // and
        EQUALITY,   // == !=
        COMPARISON, // < > <= >=
        TERM,       // + -
        FACTOR,     // * /
        UNARY,      // -x !x
        CALL,       // . ()
        PRIMARY
    };

    // Prefix/infix rules take a `canAssign` flag: true only when the expression being parsed could legally be an assignment target
    using ParseFn = void (Compiler::*)(bool canAssign);
    struct ParseRule {
        ParseFn prefix;
        ParseFn infix;
        Precedence precedence;
    };
    static const ParseRule& getRule(TokenType type);

    void parsePrecedence(Precedence precedence);
    void expression();
    void declaration();
    void varDeclaration();
    void functionDeclaration();
    void classDeclaration();
    void importDeclaration(); // import "path" as name;  ->  OP_IMPORT, then the alias is defined like a `var`
    void method();      // one method inside a class body: name "(" params ")" "{" body "}", then OP_METHOD
    void functionBody(const std::string& name, FunctionType type = FunctionType::FUNCTION, const std::string& kind = "function"); // parses "(" params ")" "{" body "}" into a NEW FunctionState; `kind` only words the error messages
    void emitReturn();  // the implicit return at the end of a body: `this` for an initializer, nil for everything else
    void statement();
    void printStatement();
    void returnStatement();
    void expressionStatement();
    void block();       // "{" declaration* "}"
    void beginScope();
    void endScope();
    void ifStatement();
    void whileStatement();
    void breakStatement();    // "break" ";"     -> discard the body's locals, jump past the loop
    void continueStatement(); // "continue" ";" -> discard the body's locals, jump to the next iteration
    void emitLoopLocalsCleanup(int loopScopeDepth); // emits the pops / upvalue-closes for every local deeper than loopScopeDepth WITHOUT forgetting them (code after the jump is still compiled in their scope)
    void forStatement();

    // Variable-declaration helpers, split out so varDeclaration() can stay agnostic about whether it's declaring a global or a local the split happens here based on scopeDepth.
    void declareVariable(const Token& name);       // records a LOCAL in the current FunctionState (no-op at global scope)
    void markInitialized();                         // marks the most recently declared local as ready to reference
    void defineVariable(size_t globalConstant);    // emits the actual OP_DEFINE_GLOBAL, or (for a local) just calls markInitialized()
    int resolveLocal(FunctionState& state, const Token& name); // returns a local's stack slot, or -1 if not a local

    // Recursively walks OUTWARD through enclosing functions to see if any of them have a local with the given name, returning its upvalue index if so, or -1 if not.
    int resolveUpvalue(int functionIndex, const Token& name);

    // Registers (or reuses, if already registered) an upvalue capture
    // for the function at `functionIndex`, returning its index in that
    // function's own upvalues list.
    int addUpvalue(int functionIndex, uint8_t index, bool isLocal);

    // Prefix/infix parse rules — each assumes the relevant token was just consumed (`previous()`), and emits bytecode for it.
    void number(bool canAssign);
    void stringLiteral(bool canAssign);
    void literal(bool canAssign);    // true / false / nil
    void grouping(bool canAssign);
    void unary(bool canAssign);
    void binary(bool canAssign);
    void variable(bool canAssign);
    void namedVariable(const Token& name, bool canAssign); // variable() for an explicit token, so `this` and `super` can reuse the local/upvalue/global resolution
    void thisExpr(bool canAssign);   // the prefix `this`
    void superExpr(bool canAssign);  // the prefix `super`, which must be followed by ".method"
    void and_(bool canAssign);
    void or_(bool canAssign);
    void call(bool canAssign);       // the infix "(" that turns a primary expression into a function call
    void arrayLiteral(bool canAssign); // the prefix "[":  [a, b, c]
    void mapLiteral(bool canAssign);   // the prefix "{":  {"a": 1, "b": 2}  (a "{" at the START of a statement is a block, handled in statement())
    void index(bool canAssign);        // the infix "[":   a[i], a[i] = v, a[i] += v
    void dot(bool canAssign);          // the infix ".":   property access (a.push)

    uint8_t argumentList(); // "(" (expression ("," expression)*)? ")" -- returns the argument count

    // Reads a variable name from `name`, adds it to the constant pool as a string, and returns its constant index
    size_t identifierConstant(const Token& name);

    // Adds `value` to the current function's constant pool (reusing an equal number or string already there) and returns its index.
    // If the pool (65536 entries) is full, reports "Too many constants in one function." (once per function) and returns 0; the compile has failed by then, so that bytecode never runs.
    size_t makeConstant(VMValue value);

    // token-stream helpers (same shape as the tree-walker's Parser)
    const Token& peek() const;
    const Token& previous() const;
    bool isAtEnd() const;
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    void consume(TokenType type, const std::string& message);
    void errorAt(const Token& token, const std::string& message);

    // bytecode emission helpers
    void emitByte(uint8_t byte);
    void emitByte(OpCode op);
    void emitConstant(VMValue value);
    // Emits `op` followed by a constant-pool index: one operand byte, or OP_WIDE before the op and a 2-byte operand when the index is above 255.
    void emitConstantOp(OpCode op, size_t constantIndex);
    int currentLine() const;

    // Emits a jump instruction with a 2-byte PLACEHOLDER offset, and returns the byte position of that placeholder so it can be fixed up later via patchJump() once the real distance is known.
    size_t emitJump(OpCode jumpOp);

    // Backpatches the jump at `jumpPlaceholderOffset` to land at the CURRENT position in the bytecode (i.e. "jump to right here").
    void patchJump(size_t jumpPlaceholderOffset);

    // Emits OP_LOOP with the (already known, since it's backward) distance back to `loopStartOffset`.
    void emitLoop(size_t loopStartOffset);
};
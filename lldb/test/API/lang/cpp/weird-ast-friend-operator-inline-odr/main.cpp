#include "plugin.h"

// The main executable's definition of 'Token'. 'operator==' is defined
// INLINE inside the class body as a 'friend' function. Such an inline
// friend function is only ever findable via argument-dependent lookup
// (ADL) on 'Token' -- it is never declared at namespace scope, and it is
// parsed as part of Token's class body, so each translation unit that
// defines 'Token' this way drags in its own uniquely-scoped 'operator=='
// FunctionDecl that is never a direct, ordinarily-looked-up child of any
// DeclContext other than the (transient) Sema state used while parsing
// the class.
//
// Note: deliberately not shared via a common header. plugin.cpp defines
// a same-named 'Token' with a genuinely different field layout ('long
// kind' + 'char tag' instead of just 'int kind') and different
// comparison semantics (it also compares the extra 'tag' field). This is
// a real ODR violation across LLDB's two modules (the executable and the
// dylib).
//
// When LLDB's DWARFASTParserClang/ASTImporter machinery has to reconcile
// both same-named 'Token' RecordDecls into the target's shared scratch
// AST context, each one drags in a distinct hidden friend 'operator=='
// FunctionDecl for what could become a single merged canonical
// RecordDecl. Sema's operator lookup for a *mixed* comparison ('ta ==
// tb') then has to pick among candidates whose parameter types
// ('const Token &') nominally name the "same" merged type but are
// actually rooted in two different (but same-named) RecordDecls with
// incompatible layouts -- exactly the kind of state that could violate
// overload-resolution invariants inside Sema::AddOverloadCandidate.
struct Token {
  int kind;
  friend bool operator==(const Token &a, const Token &b) {
    return a.kind == b.kind;
  }
};

Token ta{1};

int main() {
  plugin_init();
  plugin_entry();
  bool eq = (ta == ta);
  return eq ? 0 : 1;
}

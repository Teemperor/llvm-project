#include "plugin.h"

// See main.cpp: this dylib defines a same-named, same-layout 'Box' class
// (a field 'v' of type 'int', plus a single-argument constructor), but
// with 'operator==' declared as a genuine member function (a direct
// CXXMethodDecl child in Box's DeclContext), instead of a befriended free
// function living outside the class.
//
// This is an ODR violation: the same-named 'Box' type has two
// incompatible shapes for how 'operator==' is attached to its
// DeclContext across the executable and this dylib. When LLDB's
// ASTImporter/TypeSystemClang machinery imports/merges both 'Box'
// CXXRecordDecls into the target's shared scratch AST context, Clang's
// overload resolution (Sema::LookupOperatorOverloads) has to consider
// both the member 'operator==' (found via class-scope lookup) and the
// free, befriended 'operator==' (found via namespace-scope/ADL lookup)
// as candidates for the same merged 'Box' type -- even though, in either
// original TU, only ONE of the two ever existed for a given 'Box'.
class Box {
public:
  int v;
  bool operator==(const Box &) const;
  Box(int v_) : v(v_) {}
};

bool Box::operator==(const Box &other) const { return v == other.v; }

Box box2(42);

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Reference box2 (and its member 'operator==') so debug info for it
  // definitely gets emitted.
  bool eq = (box2 == box2);
  (void)eq;
}
}

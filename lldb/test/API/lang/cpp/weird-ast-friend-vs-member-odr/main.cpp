#include "plugin.h"

// This is the main executable's definition of 'Box': 'operator==' is a
// free function, granted access to the private field 'v' via a
// FriendDecl. This means Box's DeclContext lookup table stores the
// friend as a using-directive-like mechanism reachable via
// namespace-scope/ADL lookup, NOT as a direct child CXXMethodDecl of
// Box's DeclContext.
//
// Note: deliberately not shared via a common header. plugin.cpp defines
// a same-named, same-layout 'Box' (same private/public field 'v'), but
// with 'operator==' as a genuine member function (a direct CXXMethodDecl
// child of Box's DeclContext) instead of a befriended free function.
// This is an ODR violation across LLDB modules (the executable and the
// dylib): the same-named, same-layout 'Box' type has 'operator==' living
// in two structurally different places in the AST/DeclContext across the
// two TUs.
class Box {
  int v;
  friend bool operator==(const Box &, const Box &);

public:
  Box(int v_) : v(v_) {}
};

bool operator==(const Box &a, const Box &b) { return a.v == b.v; }

Box box1(42);

int main() {
  bool eq = (box1 == box1);
  if (eq) {
    plugin_init();
    plugin_entry();
  }
  return 0;
}

#include "plugin.h"

// 'A' and 'B' are mutually-recursive: 'A' points to 'B' and 'B' points
// back to 'A'. This is the exe's definition of the pair. 'B' is forward
// declared before 'A' is defined, mirroring how a real header would have
// to break the cycle.
struct B;

struct A {
  B *b;
  int x;
};

struct B {
  A *a;
  int y;
};

// This definition of 'A' is byte-for-byte identical to the dylib's, but
// 'B' will NOT be: the dylib's 'B' has an extra data member (see
// plugin.cpp). Because 'A' contains a 'B*' and 'B' contains an 'A*', the
// two types form an import cycle: importing 'A' requires (fully or
// partially) importing 'B' and vice versa. Injecting the ODR conflict
// into only one half of that cycle is meant to stress whatever bookkeeping
// the ASTImporter uses to track "currently being imported" decls for both
// 'A' and 'B' at once.
A g_exe_a;
B g_exe_b;

// A second exe-side pair, dedicated to being cross-linked into the
// dylib's graph below, so that the purely-exe-side cycle above
// (g_exe_a <-> g_exe_b) stays untouched and easy to reason about.
A g_exe_a2;
B g_exe_b2;

int main() {
  // Build a little graph that lives entirely in the exe:
  //   g_exe_a.b == &g_exe_b
  //   g_exe_b.a == &g_exe_a
  g_exe_a.b = &g_exe_b;
  g_exe_a.x = 1;
  g_exe_b.a = &g_exe_a;
  g_exe_b.y = 2;

  g_exe_a2.x = 3;
  g_exe_b2.y = 4;

  plugin_init();

  // Now stitch the exe's second pair and the dylib's (conflicting) pair
  // together into a single graph that crosses the module boundary in
  // both directions:
  //   g_exe_a2.b        -> dylib's B (g_plugin_b)
  //   g_plugin_b->a     -> g_exe_a2
  //   g_exe_b2.a        -> g_plugin_a
  extern A *g_plugin_a;
  extern B *g_plugin_b;
  g_exe_a2.b = g_plugin_b;
  g_plugin_b->a = &g_exe_a2;
  g_exe_b2.a = g_plugin_a;

  plugin_entry();
  return 0;
}

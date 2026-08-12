#include "plugin.h"

// 'Point' is declared inside namespace 'A', but pulled into the global
// namespace's *unqualified* lookup via a using-directive ('using namespace
// A;'), rather than being looked up namespace-qualified as 'A::Point'.
// DeclContext::lookup for an unqualified 'Point' therefore has to chain
// through the UsingDirectiveDecl that links the global namespace to 'A'.
//
// See plugin.cpp: it defines its own, ODR-conflicting 'A::Point' (three
// fields instead of two) and reaches it via the exact same using-directive
// idiom. Both modules' 'Point' resolve to 'A::Point' transitively through
// a using-directive chain rather than a direct namespace-qualified lookup,
// which is the scenario this test is exploring for ASTImporter/DeclContext
// merging bugs.
namespace A {
struct Point {
  int x, y;
};
} // namespace A
using namespace A;

void useA() {
  Point p{1, 2};
  p.x = p.x; // Break in useA: 'p' is fully initialized here.
  // Call into the dylib's 'useB' *from within* 'useA', so that 'useA's
  // stack frame (and its local 'Point p') is still live once we're
  // stopped inside 'useB'.
  plugin_entry();
}

int main() {
  plugin_init();
  useA();
  return 0;
}

#include "a.h"
#include "b.h"

// Wire up a mutual cross-dylib reference cycle:
//   a_get_wrapper()->other == b_get_type()
//   b_get_type()->other    == a_get_wrapper()
//
// Both dylib A and dylib B also privately define their OWN conflicting
// version of the *other* dylib's type (see a_private_stub.cpp and
// b_private_stub.cpp): dylib A has a private 'struct B_Type' with a
// completely different body than the real one in dylib B, and vice versa
// for dylib B's private 'struct Wrapper'. This means that once both
// dylibs' debug info is loaded, LLDB's DWARFASTParserClang/ASTImporter
// machinery sees two incompatible definitions of 'B_Type' and two
// incompatible definitions of 'Wrapper' at the same time.
void cycle_entry() {
  // By the time we get here, the cycle above is fully wired up and both
  // dylibs' private conflicting stub types have been referenced (via
  // a_touch_private_b_type()/b_touch_private_wrapper()) so they show up
  // in DWARF too.
}

int main() {
  a_init(nullptr);
  b_init(nullptr);

  Wrapper *w = a_get_wrapper();
  B_Type *bt = b_get_type();

  // Wire up the mutual cross-dylib cycle.
  w->other = bt;
  bt->other = w;

  a_touch_private_b_type();
  b_touch_private_wrapper();

  cycle_entry();
  return 0;
}

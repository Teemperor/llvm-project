#include "plugin.h"

// The main executable's mutually-recursive pair 'A'/'B', plus a
// self-referential 'using' alias that names the cyclic partner from
// inside the very type that the partner points back to:
//
//   struct A;
//   struct B { A *pa; };
//   struct A { B *pb; using SelfB = B; };
//
// 'A' contains a 'B *' and 'B' contains an 'A *', so the two RecordDecls
// form an import cycle: (fully or partially) completing either one
// requires the other. 'A::SelfB' is a type alias, declared *inside* 'A',
// that names 'B' -- the very type 'A' is mutually recursive with. This
// gives DWARFASTParserClang/TypeSystemClang a self-referential typedef
// to complete while 'A' and 'B' are both only partially known to each
// other (forward-declared pointee vs. fully defined pointer target).
//
// The dylib defines a structurally different but same-named 'A'/'B' pair
// (see plugin.cpp): 'B' there has an extra 'int extra' member and 'A'
// there has an extra 'float extraA' member, so the two modules disagree
// about the size/layout of both halves of the cycle -- a genuine ODR
// violation across the whole 'A' <-> 'B' <-> 'SelfB' knot.
struct B;

struct A {
  B *pb;
  using SelfB = B;
};

struct B {
  A *pa;
};

// A global of the (recursive, ODR-conflicting) type, mirroring the
// scenario's "global A ga;". A second global of the 'using'-aliased type
// is here purely so that clang actually emits a DW_TAG_typedef for
// 'A::SelfB' in the debug info (an alias that is never named by any
// variable, parameter, or cast is not guaranteed to get its own DWARF
// entry at -O0).
A ga;
A::SelfB *ga_selfb = nullptr;

int main() {
  ga.pb = nullptr;
  plugin_init();
  plugin_entry();
  return 0;
}

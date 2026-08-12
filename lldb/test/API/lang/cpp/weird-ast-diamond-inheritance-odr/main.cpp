#include "plugin.h"

// This is the exe's definition of the diamond hierarchy: B1 and B2 are
// joined by 'Diamond' using VIRTUAL inheritance. With virtual inheritance
// each base is only stored once (there's a single shared B1/B2 sub-object),
// and 'Diamond' additionally needs vtable pointers to find its virtual
// bases, which drastically changes the object layout compared to a
// non-virtual diamond (see plugin.cpp for the ODR-violating counterpart).
//
// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define classes called 'B1', 'B2' and 'Diamond', but the
// inheritance kind (virtual vs. non-virtual) differs between the two
// translation units. This is an ODR violation across LLDB modules (the
// executable and the dylib).
class B1 {
public:
  int b1Field = 1;
};

class B2 {
public:
  int b2Field = 2;
};

class Diamond : public virtual B1, public virtual B2 {
public:
  int diamondField = 3;
};

Diamond global_diamond;

int main() {
  // Make sure debug-info for this definition of 'Diamond' (and its virtual
  // bases) is emitted in the exe's compile unit.
  global_diamond.diamondField = global_diamond.b1Field + global_diamond.b2Field;

  plugin_init();
  plugin_entry();
  return 0;
}

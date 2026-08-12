#include "plugin.h"

// This is the exe's definition of 'Point3': all three data members are
// 'int', and the friend comparison operator is explicitly defaulted
// (C++20 "defaulted comparison operator"). A defaulted 'operator==' is
// synthesized by Sema by comparing each base and each member in
// declaration order (here: x, then y, then z).
struct Point3 {
  int x, y, z;
  friend bool operator==(const Point3 &, const Point3 &) = default;
};

// Global so debug-info for this definition of 'Point3' (including its
// defaulted comparison operator) is emitted with a full definition in the
// exe's compile unit.
Point3 point1{1, 2, 3};

// Force the compiler to actually instantiate/emit 'operator==' for this
// TU's 'Point3'. A defaulted comparison operator that is never odr-used
// is never synthesized (Sema::DefineDefaultedComparison only runs lazily,
// on first use) and is never emitted, so DWARF wouldn't even mention it
// otherwise.
volatile bool force_use_exe = (point1 == point1);

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

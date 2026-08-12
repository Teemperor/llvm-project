#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a struct named 'Meters' with a converting constructor
// and a conversion operator to 'double', but 'explicit' is on a *different*
// member in each definition:
//   - main.cpp:   the constructor is explicit, the conversion operator
//                 is NOT explicit (so an implicit Meters -> double
//                 conversion is well-formed).
//   - plugin.cpp: the constructor is NOT explicit, the conversion operator
//                 IS explicit (so an implicit Meters -> double conversion
//                 is ill-formed; only static_cast/explicit calls work).
//
// This is a genuine ODR violation: the same class name has two definitions
// that are semantically different (not just differently laid out), and
// whether a given implicit conversion is well-formed depends on which
// TU's version of the *same* member function LLDB's ASTImporter ends up
// using when merging both into the shared per-target scratch AST context.
struct Meters {
  double v;
  explicit Meters(double d) : v(d) {}
  operator double() const { return v; }
};

Meters meters_from_main(3.0);

// Force the compiler to actually emit Meters::operator double(): it would
// otherwise be dead-stripped since nothing else in this TU calls it, and
// LLDB's expression JIT needs the real symbol to exist for `expr` to
// succeed when it calls the (non-explicit, on this side) conversion.
double UseConversionOperator(const Meters &m) { return m; }

int main() {
  UseConversionOperator(meters_from_main);
  plugin_init();
  plugin_entry();
  return 0;
}

#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a struct named 'Meters' with:
//   - a user-defined conversion operator to 'double', and
//   - a befriended free 'operator+(Meters, Meters)'.
//
// The two definitions disagree on *everything* except the class name and
// the presence of those two operators:
//   - main.cpp:   'Meters' holds a single 'int mm' member. operator
//                 double() divides by 1000 (interprets 'mm' as
//                 millimeters). operator+ adds the 'mm' fields.
//   - plugin.cpp: 'Meters' holds 'double mm' plus an extra 'float
//                 precision' member (different size/layout entirely).
//                 operator double() just returns 'mm' directly (already
//                 in meters). operator+ adds the 'mm' fields and zeroes
//                 out 'precision'.
//
// This is a genuine ODR violation: the same class name has two
// definitions that are not just differently laid out, but whose
// user-defined conversion operators and friend operators mean something
// semantically different depending on which TU's version of 'Meters'
// LLDB's ASTImporter/TypeSystemClang machinery ends up using once both
// get pulled into the shared per-target scratch AST context.
//
// The conversion operator and the friend operator+ are marked
// 'noinline' so that both are guaranteed to be emitted as real,
// out-of-line functions (with real symbols) that LLDB's expression JIT
// can call, rather than being inlined away since each is used only once
// per TU.
struct Meters {
  int mm;
  __attribute__((noinline)) operator double() const { return mm / 1000.0; }
  friend __attribute__((noinline)) Meters operator+(Meters a, Meters b) {
    return {a.mm + b.mm};
  }
};

Meters ma{1000};

// Force emission of Meters::operator double() and operator+(Meters,
// Meters) as real, non-inlined, out-of-line symbols: they would
// otherwise be dead-stripped/inlined away since each is used only once
// in this TU.
__attribute__((noinline)) double UseConversionOperator(const Meters &m) {
  return m;
}
__attribute__((noinline)) Meters UsePlusOperator(Meters a, Meters b) {
  return a + b;
}

int main() {
  UseConversionOperator(ma);
  UsePlusOperator(ma, ma);
  plugin_init();
  plugin_entry();
  return 0;
}

#include "plugin.h"

// See main.cpp: this dylib's 'Meters' has the same name as the main
// executable's, but a completely different member layout ('double mm'
// plus an extra 'float precision', instead of a single 'int mm'), a
// differently-behaved conversion operator (returns 'mm' directly instead
// of dividing by 1000), and a differently-behaved friend operator+
// (zeroes out the extra 'precision' field in the result).
struct Meters {
  double mm;
  float precision;
  __attribute__((noinline)) operator double() const { return mm; }
  friend __attribute__((noinline)) Meters operator+(Meters a, Meters b) {
    return {a.mm + b.mm, 0};
  }
};

Meters mb{1.5, 0.01};

// Force emission of this module's Meters::operator double() and
// operator+(Meters, Meters) as real, non-inlined, out-of-line symbols
// (see main.cpp for why).
__attribute__((noinline)) double UseConversionOperator(const Meters &m) {
  return m;
}
__attribute__((noinline)) Meters UsePlusOperator(Meters a, Meters b) {
  return a + b;
}

extern "C" {
void plugin_init() {
  UseConversionOperator(mb);
  UsePlusOperator(mb, mb);
}

void plugin_entry() {}
}

#include "plugin.h"

// See main.cpp: this 'Meters' has the same name as the one in main.cpp,
// but 'explicit' has moved from the constructor to the conversion
// operator (the opposite of main.cpp's version).
struct Meters {
  double v;
  Meters(double d) : v(d) {}
  explicit operator double() const { return v; }
};

Meters meters_from_dylib(5.0);

// Force the compiler to actually emit Meters::operator double() (see
// main.cpp for why). Uses an explicit static_cast since operator double()
// is explicit on this (dylib) side.
double UseConversionOperator(const Meters &m) {
  return static_cast<double>(m);
}

extern "C" {
void plugin_init() { UseConversionOperator(meters_from_dylib); }

void plugin_entry() {}
}

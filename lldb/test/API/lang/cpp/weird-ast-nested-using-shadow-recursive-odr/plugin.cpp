#include "plugin.h"

// This is the "b.dylib.cpp" side of the scenario: the exact same
// double-chained using-declaration structure as main.cpp, but the
// innermost "Data" here has three members (v, w, x) instead of one, so
// this dylib's "outer::inner::Data" is an ODR-conflicting, differently
// shaped/sized redefinition of the identically-named-and-scoped struct
// from main.cpp - reached through an identically-shaped two-level
// UsingShadowDecl chain (outer::Data wraps inner::Data's shadow, and the
// global ::Data wraps outer::Data's shadow).
namespace outer {
namespace inner {
struct Data {
  int v;
  int w;
  long x;
};
} // namespace inner
using inner::Data;
} // namespace outer
using outer::Data;

Data db{1, 2, 3};

extern "C" {
void plugin_init() {}
}

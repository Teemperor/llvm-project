#include "plugin.h"

// The main executable defines 'struct Cplx' as two ints, and a matching
// 'operator+' that adds the two integer fields component-wise. See
// plugin.cpp for the dylib's incompatible definition of the same
// struct/operator pair (an actual ODR violation: same names, different
// layouts).
struct Cplx {
  int re, im;
};

Cplx operator+(Cplx a, Cplx b) { return {a.re + b.re, a.im + b.im}; }

Cplx ca{1, 2};

// a_caller() sits between main() and plugin_entry() on the stack, so that
// when we stop inside plugin_entry() (in the dylib), 'ca' (defined here,
// using the main executable's 'Cplx') is still visible by walking up one
// frame, while 'cb' (defined in the dylib, using the dylib's incompatible
// 'Cplx') is visible in the current frame. This lets a single expression
// evaluation see both conflicting definitions of 'Cplx' "live" on the
// stack at once.
void a_caller() { plugin_entry(); }

int main() {
  plugin_init();
  a_caller();
  return 0;
}

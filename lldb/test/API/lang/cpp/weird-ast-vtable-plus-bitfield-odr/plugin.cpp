#include "plugin.h"

// This is a DIFFERENT (ODR-violating) definition of 'Combo' compared to
// the one in main.cpp: here the class has TWO virtual methods ('f' and
// 'g', a bigger vtable) and a single, much wider bitfield 'x' (20 bits,
// with no 'y' at all). So relative to the exe's 'Combo' this class has
// both a different vtable shape *and* a different bitfield layout,
// stacking both ODR-conflict dimensions onto the same same-named
// RecordDecl.
class Combo {
public:
  virtual int f() { return 100; }
  virtual int g() { return 200; }
  unsigned x : 20;

  Combo() : x(123456) {}
};

Combo *gPluginCombo = 0;

extern "C" {
void plugin_init() { gPluginCombo = new Combo; }

void plugin_entry() {}
}

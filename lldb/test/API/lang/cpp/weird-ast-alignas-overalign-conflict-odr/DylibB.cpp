#include "DylibB.h"

// Plain, naturally-aligned 'Aligned': no alignas attribute, two fields
// (an 'int' and a 'long'), so its natural alignment/sizeof (16 on
// LP64 targets, once padding for 'y' is added) is completely different
// from the alignas(64), single-'int'-field 'Aligned' defined in
// DylibA.cpp/main.cpp. Same type name, mechanically incompatible layout:
// a genuine ODR violation across dylibs.
struct Aligned {
  int x;
  long y;
};

Aligned g_dylibB_aligned = {100, 200};

extern "C" {
void dylibB_init(void) {}

Aligned &dylibB_get(void) { return g_dylibB_aligned; }
}

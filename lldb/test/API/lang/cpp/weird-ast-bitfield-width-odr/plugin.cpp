#include "plugin.h"

// This definition of 'Flags' is only visible to plugin.cpp. It reuses the
// same struct name and gives the bitfield 'x' a much wider width (29 bits)
// than the one main.cpp uses for its own (conflicting) 'Flags::x'. The
// surrounding bitfields also have different names/widths so the dylib's
// notion of the byte layout of 'Flags' is substantially different from
// main.cpp's notion of the same struct name.
struct Flags {
  unsigned x : 29;
  unsigned y : 2;
  unsigned z : 1;
};

Flags gPluginFlags = {123456789, 1, 0};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}

#include "plugin.h"

// This definition of 'Flags' is only visible to main.cpp. It gives the
// bitfield 'x' a narrow width (3 bits) and packs a bunch of differently
// named/sized bitfields around it so the overall byte layout (and the
// size of the struct) differs substantially from the dylib's definition
// of the (same-named) 'Flags' struct below.
struct Flags {
  unsigned a : 5;
  unsigned x : 3;
  unsigned b : 7;
  unsigned c : 1;
};

Flags gMainFlags = {1, 2, 3, 1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

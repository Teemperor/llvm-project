#include "plugin.h"

// Same field list as the dylib's "Packed", but with the packed attribute
// applied. This changes the alignment/padding rules for the struct so that
// its size and member offsets differ significantly from the unpacked
// version defined for the exact same type name in the dylib.
struct __attribute__((packed)) Packed {
  char a;
  int b;
  char c;
  long d;
};

Packed g_main_packed = {1, 2, 3, 4};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

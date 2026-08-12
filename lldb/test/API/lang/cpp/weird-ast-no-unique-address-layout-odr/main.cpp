#include "plugin.h"

// Same field list as the dylib's "Holder", but the empty "e" member is
// marked [[no_unique_address]]. This lets the compiler overlap "e" with
// the tail padding of the struct, so "Holder" only needs 4 bytes here
// (just enough for "x") instead of 8. This changes the byte offset of
// "x" (0 here) relative to the identically-named, identically-fielded
// "Holder" in the dylib (4 there), even though the source-level field
// list looks the same.
struct Empty {};
struct Holder {
  [[no_unique_address]] Empty e;
  int x;
};

Holder g_main_holder = {{}, 42};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

#include "plugin.h"

// Same field names/types as main.cpp's "Holder", but without the
// [[no_unique_address]] attribute on "e". Since "e" is a plain,
// non-[[no_unique_address]] member here, the compiler must give it its
// own address distinct from "x", which forces "x" to start at offset 4
// (after padding for "e") instead of offset 0. This makes this "Holder"
// 8 bytes instead of 4, even though it has the exact same member list
// (by name and type) as main.cpp's "Holder".
struct Empty {};
struct Holder {
  Empty e;
  int x;
};

Holder g_dylib_holder = {{}, 99};

extern "C" {
void plugin_init(void) {}

void plugin_entry(void) {}
}

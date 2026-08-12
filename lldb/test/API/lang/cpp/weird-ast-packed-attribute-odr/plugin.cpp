#include "plugin.h"

// Same field list as main.cpp's "Packed", but without the packed attribute.
// The compiler is free to insert padding between/after the fields to
// satisfy natural alignment, so this type's size and layout differ
// significantly from the identically-named type in the main executable.
struct Packed {
  char a;
  int b;
  char c;
  long d;
};

Packed g_dylib_packed = {5, 6, 7, 8};

extern "C" {
void plugin_init(void) {}

void plugin_entry(void) {}
}

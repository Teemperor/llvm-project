#include "plugin.h"

// Large version of the "typedef struct { ... } Foo;" idiom as seen by the
// dylib. Same typedef name ("Foo"), but the underlying anonymous struct has
// a completely different shape (three members instead of one, with a mix
// of types), and therefore a different size, than the one in main.cpp.
// Since both structs are unnamed and only reachable through the shared
// typedef name "Foo", this is the anonymous-struct-via-typedef analogue of
// a classic named-tag ODR conflict.
typedef struct {
  int a;
  int b;
  long c;
} Foo;

Foo plugin_foo = {2, 3, 4};

extern "C" {
void plugin_init() { plugin_foo.c = 5; }

void plugin_entry() {}
}

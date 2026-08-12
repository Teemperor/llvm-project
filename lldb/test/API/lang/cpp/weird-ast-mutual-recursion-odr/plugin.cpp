#include "plugin.h"

// Same mutually-recursive shape as main.cpp's 'A'/'B', but 'B' here has
// an EXTRA data member ('extra') compared to the exe's 'B'. 'A' itself is
// textually identical between the two translation units. Because 'A' and
// 'B' reference each other, this single-field ODR conflict on 'B' taints
// the whole 'A' <-> 'B' cycle: any attempt to import 'A' from this module
// transitively needs 'B', whose definition disagrees with the one already
// imported from the exe (and vice versa).
struct B;

struct A {
  B *b;
  int x;
};

struct B {
  A *a;
  int y;
  int extra;
};

A *g_plugin_a = 0;
B *g_plugin_b = 0;

extern "C" {
void plugin_init() {
  g_plugin_a = new A;
  g_plugin_b = new B;

  g_plugin_a->b = g_plugin_b;
  g_plugin_a->x = 10;

  g_plugin_b->a = g_plugin_a;
  g_plugin_b->y = 20;
  g_plugin_b->extra = 99;
}

void plugin_entry() {}
}

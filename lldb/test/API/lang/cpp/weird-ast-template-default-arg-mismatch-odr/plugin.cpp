#include "plugin.h"

// See main.cpp: this 'Grid' has the same name as the one in main.cpp, and
// is instantiated via the same bare 'Grid<int>' spelling, but its default
// for 'N' (and therefore the actual instantiated specialization) differs:
// 'Grid<int, 8>' here vs. 'Grid<int, 4>' in main.cpp.
template <typename T, int N = 8> struct Grid {
  T cells[N];
};

Grid<int> plugin_g = {{5, 6, 7, 8, 9, 10, 11, 12}};

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Set breakpoint here.
}
}

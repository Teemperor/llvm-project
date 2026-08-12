#include "DylibA.h"
#include "DylibB.h"

// The main executable's own definition of 'Aligned' matches DylibA.cpp's
// definition exactly (alignas(64), single 'int' field), so that the
// global array below and DylibA's global array share a layout, while
// DylibB's same-named 'Aligned' (a plain, two-field struct) is a
// completely different, ODR-violating shape.
struct alignas(64) Aligned {
  int x;
};

// A small global array of the cache-line-aligned 'Aligned', analogous to
// DylibA's g_dylibA_aligned. Pointer subtraction between elements of this
// array uses sizeof(Aligned) as computed for *this* (64-byte) layout.
Aligned arr[4] = {{10}, {20}, {30}, {40}};

// A convenient function to set a breakpoint on: by the time control
// reaches here, both dylibs have been initialized and both conflicting
// 'Aligned' definitions (DylibA's/main's 64-byte one-field layout, and
// DylibB's naturally-aligned two-field layout) are reachable from
// expressions evaluated at this line.
void entry() {}

int main() {
  dylibA_init();
  dylibB_init();
  entry();
  return 0;
}

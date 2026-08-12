#include "plugin.h"

// See main.cpp: this is the same using-directive idiom ('namespace A {
// struct Point { ... }; } using namespace A;') as in main.cpp, but this
// module's 'A::Point' has an extra field ('z') compared to main.cpp's
// two-field 'A::Point', giving 'A::Point' two genuinely conflicting
// layouts across the two modules while both still reach 'Point'
// unqualified through a using-directive rather than a direct
// namespace-qualified lookup.
namespace A {
struct Point {
  int x, y, z;
};
} // namespace A
using namespace A;

void useB() {
  Point p{1, 2, 3};
  p.x = p.x; // Break in useB: 'p' is fully initialized here.
}

extern "C" {
void plugin_init() {}

void plugin_entry() { useB(); }
}

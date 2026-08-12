#include "plugin.h"

// See main.cpp: this 'geo::Point' has the same name as main.cpp's, but a
// genuinely different layout (three 'int' members instead of two), and
// this 'geo::translate' overload takes an extra 'dy' offset argument to
// match. 'pb' is this file's 3-field 'Point', analogous to main.cpp's
// 2-field 'pa'.
namespace geo {
struct Point {
  int x, y, z;
};
Point translate(Point p, int dx, int dy) {
  p.x += dx;
  p.y += dy;
  return p;
}
} // namespace geo

geo::Point pb{0, 0, 0};

extern "C" {
void plugin_init(void) {}

void plugin_entry(void) {}
}

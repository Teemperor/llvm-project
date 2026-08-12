#include "plugin.h"

struct Shape {
  double w, h, d;
};

Shape gShape2 = {1.5, 2.5, 3.5};

extern "C" void dylib2_entry() {
  double x = gShape2.w;
  (void)x;
}

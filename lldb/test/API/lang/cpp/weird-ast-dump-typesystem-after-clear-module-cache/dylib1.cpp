#include "plugin.h"

struct Shape {
  int tag;
  int value;
};

Shape gShape1 = {1, 100};

extern "C" void dylib1_entry() {
  int x = gShape1.value;
  (void)x;
}

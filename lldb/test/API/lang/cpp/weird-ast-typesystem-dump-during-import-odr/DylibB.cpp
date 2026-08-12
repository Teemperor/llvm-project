#include "DylibB.h"

extern "C" {
void dylib_b_entry(void *p) {
  Shared *s = (Shared *)p;
  s->b = 2;
}
}

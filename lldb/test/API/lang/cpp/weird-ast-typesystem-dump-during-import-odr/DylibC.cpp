#include "DylibC.h"

extern "C" {
void dylib_c_entry(void *p) {
  Shared *s = (Shared *)p;
  s->c = 3;
}
}

#include "d2.h"

extern "C" {
Ring *d2_make() {
  Ring *r = new Ring();
  r->next = nullptr;
  r->payload = 222.0;
  return r;
}

void d2_set_next(Ring *self, Ring *next) { self->next = next; }
}

#include "d1.h"

extern "C" {
Ring *d1_make() {
  Ring *r = new Ring();
  r->next = nullptr;
  r->payload = 111;
  return r;
}

void d1_set_next(Ring *self, Ring *next) { self->next = next; }
}

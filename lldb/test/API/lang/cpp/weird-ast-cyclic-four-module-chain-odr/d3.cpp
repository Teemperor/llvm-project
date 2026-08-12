#include "d3.h"

extern "C" {
Ring *d3_make() {
  Ring *r = new Ring();
  r->next = nullptr;
  r->payload.a = 333;
  r->payload.b = 334;
  return r;
}

void d3_set_next(Ring *self, Ring *next) { self->next = next; }
}

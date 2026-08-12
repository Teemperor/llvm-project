#include "d4.h"

extern "C" {
Ring *d4_make() {
  Ring *r = new Ring();
  r->next = nullptr;
  r->payload = (void *)0x444;
  return r;
}

void d4_set_next(Ring *self, Ring *next) { self->next = next; }
}

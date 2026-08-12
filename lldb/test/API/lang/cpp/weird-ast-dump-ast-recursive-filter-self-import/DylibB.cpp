#include "DylibB.h"

// A separate 3-node circular linked list, private to dylib B, built
// fresh (and re-linked into a cycle) every time dylibB_make_ring() is
// called. Uses dylib B's own (ODR-conflicting) 'app::Ring' shape, which
// has an extra 'tag' field that dylib A's shape (see DylibA.cpp) does
// not have.
static app::Ring gRingB1;
static app::Ring gRingB2;
static app::Ring gRingB3;

extern "C" {
void dylibB_init() {}

app::Ring *dylibB_make_ring() {
  gRingB1.val = 10;
  gRingB2.val = 20;
  gRingB3.val = 30;
  gRingB1.tag = 100;
  gRingB2.tag = 200;
  gRingB3.tag = 300;
  gRingB1.next = &gRingB2;
  gRingB2.next = &gRingB3;
  gRingB3.next = &gRingB1;
  return &gRingB1;
}
}

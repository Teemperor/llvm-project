#include "DylibA.h"

// A separate 3-node circular linked list, private to dylib A, built
// fresh (and re-linked into a cycle) every time dylibA_make_ring() is
// called.
static app::Ring gRingA1;
static app::Ring gRingA2;
static app::Ring gRingA3;

extern "C" {
void dylibA_init() {}

app::Ring *dylibA_make_ring() {
  gRingA1.val = 1;
  gRingA2.val = 2;
  gRingA3.val = 3;
  gRingA1.next = &gRingA2;
  gRingA2.next = &gRingA3;
  gRingA3.next = &gRingA1;
  return &gRingA1;
}
}

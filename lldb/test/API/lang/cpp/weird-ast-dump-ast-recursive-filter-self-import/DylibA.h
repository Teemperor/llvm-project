#ifndef DYLIBA_H_IN
#define DYLIBA_H_IN

// Dylib A's shape for 'app::Ring': self-referential via 'next', two
// fields (no 'tag'). See DylibB.h for the conflicting ODR-violating
// shape (same qualified name, same first two fields, but with an extra
// trailing 'tag' field) that dylib B defines independently.
namespace app {
struct Ring {
  app::Ring *next;
  int val;
};
} // namespace app

extern "C" {
void dylibA_init(void);

// Builds a fresh 3-node circular linked list of dylib A's 'app::Ring'
// (gRingA1 -> gRingA2 -> gRingA3 -> gRingA1 -> ...) and returns the head.
app::Ring *dylibA_make_ring(void);
}

#endif // DYLIBA_H_IN

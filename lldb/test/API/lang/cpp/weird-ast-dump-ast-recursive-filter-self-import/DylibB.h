#ifndef DYLIBB_H_IN
#define DYLIBB_H_IN

// Dylib B's shape for 'app::Ring': same qualified name and same first
// two fields as dylib A's (see DylibA.h), but self-referential via
// 'next' AND with one extra trailing 'int tag' field. Same name, same
// leading field layout, differing only by this one extra field --
// a classic genuine ODR violation across two independently-compiled
// modules, on a self-referential (pointer-to-self) record.
namespace app {
struct Ring {
  app::Ring *next;
  int val;
  int tag;
};
} // namespace app

extern "C" {
void dylibB_init(void);

// Builds a fresh, separate 3-node circular linked list of dylib B's
// (differently-shaped) 'app::Ring' (gRingB1 -> gRingB2 -> gRingB3 ->
// gRingB1 -> ...) and returns the head.
app::Ring *dylibB_make_ring(void);
}

#endif // DYLIBB_H_IN

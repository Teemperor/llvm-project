// The four dylibs below (d1.h/d2.h/d3.h/d4.h) each define a struct named
// 'Ring' with a mutually incompatible layout -- all four agree on a leading
// 'Ring *next' field, but diverge on the second 'payload' field:
//   - d1: int payload;
//   - d2: double payload;
//   - d3: struct { int a, b; } payload;
//   - d4: void *payload;
//
// The main executable below intentionally never sees any of those
// definitions: it only forward declares 'Ring' so that it can hold one
// pointer per dylib. At runtime the four nodes are linked into a genuine
// circular linked list: g1->next == g2, g2->next == g3, g3->next == g4,
// g4->next == g1, so walking ->next four times from any node returns to
// that same node. This is meant to stress the ASTImporter's "already
// imported" bookkeeping: reconciling the same tag name 'Ring' pulled in
// from four different modules along a cycle that returns to its own
// starting point, rather than a simple DAG of imports.
struct Ring;

extern "C" {
Ring *d1_make(void);
Ring *d2_make(void);
Ring *d3_make(void);
Ring *d4_make(void);
void d1_set_next(Ring *self, Ring *next);
void d2_set_next(Ring *self, Ring *next);
void d3_set_next(Ring *self, Ring *next);
void d4_set_next(Ring *self, Ring *next);
}

// One node from each of the four dylibs.
Ring *g1 = nullptr;
Ring *g2 = nullptr;
Ring *g3 = nullptr;
Ring *g4 = nullptr;

void ring_entry() {
  // By the time we get here, g1 -> g2 -> g3 -> g4 -> g1 forms a real
  // circular linked list at runtime, one node from each of the four
  // dylibs, each node's static type being an incompatible ODR-violating
  // 'struct Ring'.
}

int main() {
  g1 = d1_make();
  g2 = d2_make();
  g3 = d3_make();
  g4 = d4_make();

  d1_set_next(g1, g2);
  d2_set_next(g2, g3);
  d3_set_next(g3, g4);
  d4_set_next(g4, g1);

  ring_entry();
  return 0;
}

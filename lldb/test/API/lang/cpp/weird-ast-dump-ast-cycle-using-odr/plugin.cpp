#include "plugin.h"

// The dylib's 'A'/'B' pair: same names, same mutually-recursive shape,
// and the same self-referential 'using SelfB = B;' alias as main.cpp's
// pair -- but both halves of the cycle disagree with the exe's in size:
// this 'B' has an extra 'int extra' member, and this 'A' has an extra
// 'float extraA' member. Combined with the mutual pointer recursion and
// the 'SelfB' alias declared inside 'A' that names 'B' right back, this
// taints the whole 'A' <-> 'B' <-> 'SelfB' knot with a genuine ODR
// violation across the module boundary.
struct B;

struct A {
  B *pb;
  using SelfB = B;
  float extraA;
};

struct B {
  A *pa;
  int extra;
};

// The dylib's global, mirroring the scenario's "global A gb;". As in
// main.cpp, a second global of the aliased type forces clang to emit a
// DW_TAG_typedef for 'A::SelfB' here too.
A gb;
A::SelfB *gb_selfb = nullptr;

extern "C" {
void plugin_init() {
  gb.pb = nullptr;
  gb.extraA = 1.5f;
}

void plugin_entry() {}
}

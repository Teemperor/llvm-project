#include "plugin.h"

// This dylib's version of 'Shared' has a single 'int' member. DylibTwo.cpp
// (loaded into the very same process) defines an identically-named
// 'Shared' whose single member is a 'float' instead -- a deliberate ODR
// violation where two CXXRecordDecls share a name and a superficially
// similar shape (one data member called 'a') but disagree about that
// member's fundamental type (and therefore, on most targets, its bit
// pattern even though the size happens to match).
struct Shared {
  int a;
};

Shared *gShared1 = nullptr;

extern "C" {

void dylib1_init() {
  static Shared s{1};
  gShared1 = &s;
}

} // extern "C"

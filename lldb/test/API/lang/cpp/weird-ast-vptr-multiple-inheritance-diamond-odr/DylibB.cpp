#include "DylibB.h"

void GrandBase::f() {}

Derived *gDerivedB = nullptr;

extern "C" {
void dylibB_init() {
  gDerivedB = new Derived();
  // Derived has two distinct GrandBase subobjects here (via Mid1 and via
  // Mid2), so they need to be set independently.
  gDerivedB->Mid1::gb = 200;
  gDerivedB->Mid2::gb = 201;
  gDerivedB->m1 = 202;
  gDerivedB->m2 = 203;
  gDerivedB->d = 204;
}
}

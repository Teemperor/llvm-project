#include "DylibA.h"

void GrandBase::f() {}

Derived *gDerivedA = nullptr;

extern "C" {
void dylibA_init() {
  gDerivedA = new Derived();
  gDerivedA->gb = 100;
  gDerivedA->m1 = 101;
  gDerivedA->m2 = 102;
  gDerivedA->d = 103;
}
}

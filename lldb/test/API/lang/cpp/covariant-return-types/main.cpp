#include "main.h"

int main() {
  Derived derived;
  Base base;
  Base *base_ptr_to_derived = &derived;
  (void)base_ptr_to_derived->getPtr();
  (void)base_ptr_to_derived->getRef();
  (void)base_ptr_to_derived->getOtherPtr();
  (void)base_ptr_to_derived->getOtherRef();

  ReferencingDerived referencing_derived;
  return 0; // break here
}

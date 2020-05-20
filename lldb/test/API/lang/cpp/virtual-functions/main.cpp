// Tests virtual function calls. As virtual destructors influence
// vtables this tests also needs to cover all combinations of
// virtual destructors in the derived/base class.

#include "main.h"

int main() {
  // Declare base classes.
  BaseWithVirtDtor base_with_dtor;
  BaseWithoutVirtDtor base_without_dtor;

  // Declare all the derived classes.
  DerivedWithVirtDtor derived_with_dtor;
  DerivedWithoutVirtDtor derived_without_dtor;
  DerivedWithBaseVirtDtor derived_with_base_dtor;
  DerivedWithVirtDtorButNoBaseDtor derived_with_dtor_but_no_base_dtor;
  DerivedWithOverload derived_with_overload;
  DerivedWithOverloadAndUsing derived_with_overload_and_using;

  // The previous classes as their base class type.
  BaseWithVirtDtor &derived_with_dtor_as_base = derived_with_dtor;
  BaseWithoutVirtDtor &derived_without_as_base = derived_without_dtor;
  BaseWithVirtDtor &derived_with_base_dtor_as_base = derived_with_base_dtor;
  BaseWithoutVirtDtor &derived_with_dtor_but_no_base_dtor_as_base = derived_with_dtor_but_no_base_dtor;

  // Call functions so that they are compiled.
  int i = base_with_dtor.foo() + base_without_dtor.foo() +
          derived_with_dtor.foo() + derived_without_dtor.foo() +
          derived_with_base_dtor.foo() + derived_with_overload.foo(1)
          + derived_with_overload_and_using.foo(2)
          + derived_with_overload_and_using.foo();

  return i; // break here
}

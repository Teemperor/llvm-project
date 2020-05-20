#include "main.h"

int main() {
  ClassWithImplicitCtor C1;
  C1.foo();
  ClassWithDefaultedCtor C2;
  C2.foo();
  ClassWithOneCtor C3(22);
  ClassWithMultipleCtor C4(23);
  ClassWithMultipleCtor C5(24, 25);
  ClassWithDeletedCtor C6;
  ClassWithDeletedDefaultCtor C7(26);

  return 0; // break here
}

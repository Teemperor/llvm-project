#include <stdio.h>

class A {
public:
  static int getStaticValue();
  int getMemberValue();
  int a;
};

int A::getStaticValue() { return 5; }

int A::getMemberValue() { return a; }

#include "main.h"

int main() {
  C c;
  const C const_c;
  c.func();
  c.nonconst_func();
  const_c.func();
  c.const_func();
  return 0; // break here
}

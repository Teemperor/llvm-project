#include "main.h"

int main() {
  FinalClass C;
  // Call functions so they get emitted.
  C.func1();
  C.func2();
  C.final_func();
  C.func_common();
  C.Base1::func_base();
  return 0; // break here
}

// Main links all six 'M<N>' dylibs. It forward declares 'struct Common'
// itself and only ever sees opaque 'Common *' pointers obtained by calling
// each dylib's 'MakeCommon<N>()' factory -- so main never sees any of the
// six conflicting definitions of 'Common' (each with a different array
// bound on 'fields', see common.h.in) at compile time. Those six
// conflicting definitions only get pulled into LLDB's shared per-target
// scratch AST context when the test's expression(s)/dump commands run.
#include "plugin.h"

Common *m1, *m2, *m3, *m4, *m5, *m6;

void main_entry() {
  m1 = MakeCommon1();
  m2 = MakeCommon2();
  m3 = MakeCommon3();
  m4 = MakeCommon4();
  m5 = MakeCommon5();
  m6 = MakeCommon6();
  int done = 1; // Set breakpoint here, after all six globals are populated.
  (void)done;
}

int main() {
  main_entry();
  return 0;
}

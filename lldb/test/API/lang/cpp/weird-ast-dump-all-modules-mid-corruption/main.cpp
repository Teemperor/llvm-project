#include "plugin.h"

// Empty function used purely as a breakpoint location: by the time we hit
// it both dylibs have been loaded and both dylib*_init() calls have run,
// so gShared1 (DylibOne's int-typed 'Shared') and gShared2 (DylibTwo's
// float-typed 'Shared') are both set up and their debug info is available
// to LLDB, together with the main executable's own debug info, at the
// same time.
void main_entry() {}

int main() {
  dylib1_init();
  dylib2_init();
  main_entry();
  return 0;
}

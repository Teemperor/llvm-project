#include "plugin.h"

// Empty function used purely as a breakpoint location: by the time we hit
// it both dylibs have been loaded and both dylib_*_init() calls have run,
// so gSharedOne (DylibOne's pure-virtual-'go' 'Shared') and gSharedTwo
// (DylibTwo's non-pure-'go' 'Shared') are both set up and their debug info
// is available to LLDB at the same time.
void main_entry() {}

int main() {
  dylib_one_init();
  dylib_two_init();
  main_entry();
  return 0;
}

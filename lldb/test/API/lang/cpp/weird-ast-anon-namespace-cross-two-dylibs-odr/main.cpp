#include "dylib_one.h"
#include "dylib_two.h"

// Empty function used purely as a breakpoint location: by the time we hit
// it both dylibs have been loaded and both dylib_*_init() calls have run,
// so g_one (from dylib_one) and g_two (from dylib_two) are both set up and
// their debug info is available to LLDB at the same time.
void main_entry() {}

int main() {
  dylib_one_init();
  dylib_two_init();
  main_entry();
  return 0;
}

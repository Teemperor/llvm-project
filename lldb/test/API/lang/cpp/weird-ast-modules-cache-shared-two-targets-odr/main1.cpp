#include "Common.h"

// Only CommonModExe1 is on the include/module search path used for this
// translation unit (see Makefile), so this pulls in that "struct Cfg"
// (just "int mode") and bakes its definition into exe1's own PCM
// (-fmodules -gmodules). exe2's main2.cpp -- built as a completely
// separate executable below -- independently defines an incompatible
// "struct Cfg" (with an extra "int flags" member) under the very same
// module name.
Cfg gCfg1 = makeCfg(11);

int main() {
  Cfg local1 = makeCfg(111);
  return local1.mode; // break here
}

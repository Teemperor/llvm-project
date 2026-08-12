#include "Common.h"

// Only CommonModExe2 is on the include/module search path used for this
// translation unit (see Makefile), so this pulls in CommonModExe2's
// "struct Cfg" ("int mode" + "int flags") and bakes its definition into
// exe2's own PCM. This is a real ODR violation across two entirely
// separate executables/targets that nonetheless happen to share the very
// same on-disk clang-module-cache directory once both are loaded into
// the same SBDebugger (see the test's use of
// "settings set symbols.clang-modules-cache-path").
Cfg gCfg2 = makeCfg(22, 222);

int main() {
  Cfg local2 = makeCfg(2222, 22222);
  return local2.mode; // break here
}

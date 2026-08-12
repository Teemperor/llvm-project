#ifndef COMMON_H_IN
#define COMMON_H_IN

// This "CommonMod" module is only ever visible while building exe2's
// main.cpp (see Makefile: exe2's main.o gets -I .../CommonModExe2). It
// deliberately declares its own, incompatible "struct Cfg" under the very
// same module name ("CommonMod") and the very same header name
// ("Common.h") as CommonModExe1/Common.h, but with an extra member. Since
// both executables are debugged in the same SBDebugger while sharing a
// single on-disk clang-module-cache directory (see the test's use of
// "settings set symbols.clang-modules-cache-path"), this is meant to
// stress-test how LLDB keys/validates its per-target module cache entries
// when two unrelated targets' PCMs could plausibly alias each other on
// disk.
struct Cfg {
  int mode;
  int flags;
};

inline Cfg makeCfg(int mode, int flags) { return Cfg{mode, flags}; }

#endif // COMMON_H_IN

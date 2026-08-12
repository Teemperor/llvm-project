#ifndef COMMON_H_IN
#define COMMON_H_IN

// This "CommonMod" module is only ever visible while building exe1's
// main.cpp (see Makefile: exe1's main.o gets -I .../CommonModExe1). It
// defines "struct Cfg" with a single member.
struct Cfg {
  int mode;
};

inline Cfg makeCfg(int mode) { return Cfg{mode}; }

#endif // COMMON_H_IN

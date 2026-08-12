#ifndef MODULE_B_SHARED_H_IN
#define MODULE_B_SHARED_H_IN

// Deliberately named identically to ModuleA's "struct Shared", but with a
// completely different layout (extra members, different types). This
// module is only visible while building plugin.cpp, so this is the only
// definition of "Shared" that ends up inside ModuleB's PCM.
struct Shared {
  double moduleBTag;
  double moduleBValue;
  double moduleBExtra;
};

inline Shared makeSharedFromModuleB(double value) {
  return Shared{0xB, value, value * 2};
}

#endif // MODULE_B_SHARED_H_IN

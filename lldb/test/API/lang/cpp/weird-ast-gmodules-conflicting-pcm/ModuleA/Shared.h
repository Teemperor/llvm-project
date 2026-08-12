#ifndef MODULE_A_SHARED_H_IN
#define MODULE_A_SHARED_H_IN

// This 'Shared' struct is only ever seen by the compiler while it is
// building main.cpp (through ModuleA's own private module map). ModuleB
// below declares an entirely unrelated struct with the very same name,
// so the two PCMs each end up with their own, incompatible AST node for
// "struct Shared".
struct Shared {
  int moduleATag;
  int moduleAValue;
};

inline Shared makeSharedFromModuleA(int value) {
  return Shared{0xA, value};
}

#endif // MODULE_A_SHARED_H_IN

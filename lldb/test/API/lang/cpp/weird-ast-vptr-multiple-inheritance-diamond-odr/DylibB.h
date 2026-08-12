#ifndef DYLIB_B_H_IN
#define DYLIB_B_H_IN

// Same class names as DylibA.h, but Mid1/Mid2 inherit from GrandBase
// *non-virtually* here. That makes this a classic (non-virtual) diamond:
// Derived now contains *two* separate GrandBase subobjects (one via Mid1,
// one via Mid2) instead of the single shared one DylibA.h's virtual
// inheritance produces. Merging DWARF for this 'Derived'/'Mid1'/'Mid2'/
// 'GrandBase' with DylibA's under the same names in LLDB's shared scratch
// AST context creates a Frankenstein set of RecordDecls where virtual-base
// layout assumptions from one definition can end up applied to the
// non-virtual layout of the other (or vice versa).
struct GrandBase {
  virtual void f();
  int gb;
};
struct Mid1 : GrandBase {
  int m1;
};
struct Mid2 : GrandBase {
  int m2;
};
struct Derived : Mid1, Mid2 {
  int d;
};

extern "C" {
void dylibB_init(void);
}

#endif // DYLIB_B_H_IN

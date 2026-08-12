#ifndef DYLIB_A_H_IN
#define DYLIB_A_H_IN

// GrandBase is a single, virtually-shared base of Derived: Mid1 and Mid2
// both inherit from it *virtually*, so a Derived object contains exactly
// one GrandBase subobject and needs a virtual-base-offset entry (vbase
// offset / VTT) to find it from either Mid1 or Mid2.
struct GrandBase {
  virtual void f();
  int gb;
};
struct Mid1 : virtual GrandBase {
  int m1;
};
struct Mid2 : virtual GrandBase {
  int m2;
};
struct Derived : Mid1, Mid2 {
  int d;
};

extern "C" {
void dylibA_init(void);
}

#endif // DYLIB_A_H_IN

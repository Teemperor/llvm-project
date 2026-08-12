#include "plugin.h"
#include <cstdio>

// This is a DIFFERENT (ODR-violating) definition of 'Base' compared to the
// one in main.cpp: here neither the destructor nor 'f' is virtual, so this
// class has no vtable pointer and is *not* a dynamic class at all, unlike
// the exe's polymorphic 'Base' (see main.cpp). The two same-named
// CXXRecordDecls therefore have different sizes (this one is 8 bytes
// smaller, missing the vtable pointer) and disagree on whether
// isDynamicClass() should be true.
struct Base {
  ~Base() {}
  void f() {}
  int x = 1;
};

extern "C" {
void plugin_init(void) {}

void plugin_entry(void) {
  // A local, non-polymorphic 'Base' instance, so the dylib's conflicting
  // definition of 'Base' also has debug info describing it and can be
  // referenced from an expression evaluated while stopped here.
  Base b;
  b.f();
  printf("plugin_entry\n");
}
}

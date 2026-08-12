#include "plugin.h"

// This is a DIFFERENT (ODR-violating) definition of 'Base'/'Derived'
// compared to the one in main.cpp: here the destructor is NOT noexcept,
// but 'f' IS noexcept -- i.e. the noexcept-ness of the two virtual member
// functions on the same vtable is swapped relative to the exe's
// definition. 'Derived' overrides the destructor in both definitions, so
// evaluating an expression that references both the exe's and the
// dylib's globals forces LLDB's type-system machinery to reconcile two
// CXXRecordDecls for 'Base' (and 'Derived') that only disagree on
// exception specifications of virtual functions occupying the very same
// vtable slots.
struct Base {
  virtual ~Base();
  virtual void f() noexcept;
};

Base::~Base() {}
void Base::f() noexcept {}

struct Derived : Base {
  ~Derived() override;
};

Derived::~Derived() {}

Derived *gPluginDerived = 0;
Base *gPluginBase = 0;

extern "C" {
void plugin_init() {
  gPluginDerived = new Derived;
  gPluginBase = gPluginDerived;
}

void plugin_entry() {}
}

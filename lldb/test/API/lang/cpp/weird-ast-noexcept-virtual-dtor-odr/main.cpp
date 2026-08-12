#include "plugin.h"

// This is the exe's definition of 'Base'/'Derived'. 'Base' declares a
// virtual, noexcept destructor and a plain (not noexcept) virtual method
// 'f'. The dylib below declares the identical-looking 'Base'/'Derived'
// pair, but with the noexcept-ness of the destructor and 'f' *swapped*
// (see plugin.cpp). This is a deliberate ODR violation: two CXXRecordDecls
// named 'Base' (and 'Derived') whose virtual member functions only differ
// in their exception specification, which Clang models as part of the
// FunctionProtoType's ExtProtoInfo. Overriding-function exception-spec
// compatibility (Sema::CheckOverridingFunctionExceptionSpec) is normally
// checked once, at parse time, for a single consistent definition; LLDB's
// ASTImporter/TypeSystemClang machinery instead fabricates these
// definitions post-hoc from DWARF and can end up merging/reconciling two
// CXXRecordDecls for the same vtable slot with mismatched exception specs.
struct Base {
  virtual ~Base() noexcept;
  virtual void f();
};

Base::~Base() noexcept {}
void Base::f() {}

struct Derived : Base {
  ~Derived() override;
};

Derived::~Derived() {}

Derived global_derived;

int main() {
  // Make sure debug-info for this definition of 'Base'/'Derived' is
  // emitted in the exe's compile unit, and that the vtable-holding
  // globals are actually used (so the linker keeps them around).
  global_derived.f();

  plugin_init();
  plugin_entry();
  return 0;
}

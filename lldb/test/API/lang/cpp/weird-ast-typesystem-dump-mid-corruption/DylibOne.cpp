#include "plugin.h"

// This dylib's version of the abstract base class 'Shared': the pure
// virtual method 'go' is a genuine pure virtual (no body), so this
// CXXRecordDecl is a true abstract class -- 'Shared' can never be
// instantiated directly here, and the vtable slot for 'go' is filled with
// Itanium's "pure virtual called" trap thunk rather than a real function.
//
// DylibTwo.cpp (loaded into the very same process) defines an
// identically-named 'Shared' with the exact same virtual method signature,
// but gives 'go' a real (non-pure) body -- see the comment there. This is
// a deliberate ODR violation: two CXXRecordDecls named 'Shared', with the
// same vtable shape, but where the DefinitionData's "abstract" bit and the
// underlying vtable slot contents fundamentally disagree about whether the
// class is instantiable.
struct Shared {
  virtual void go() = 0;
  virtual ~Shared() {}
};

struct ConcreteOne : Shared {
  void go() override {}
};

ConcreteOne *gConcreteOne = nullptr;
Shared *gSharedOne = nullptr;

extern "C" {

void dylib_one_init() {
  gConcreteOne = new ConcreteOne;
  gSharedOne = gConcreteOne;
}

} // extern "C"

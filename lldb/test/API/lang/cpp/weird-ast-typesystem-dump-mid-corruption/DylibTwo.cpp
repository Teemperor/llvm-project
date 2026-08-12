#include "plugin.h"

// This dylib's version of 'Shared' is spelled identically to DylibOne.cpp's
// 'Shared' (same name, same single virtual method 'go', same virtual
// destructor), but here 'go' has a real, non-pure body. So, unlike
// DylibOne's 'Shared', this 'Shared' is a perfectly ordinary, instantiable
// polymorphic class -- its DefinitionData is NOT abstract, and the vtable
// slot for 'go' points at real code instead of a pure-virtual-called trap.
//
// Loading both dylibs into the same process and forcing LLDB to import
// both same-named 'Shared' definitions into the shared per-target scratch
// ASTContext (once via each dylib's own module) means the ASTImporter has
// to reconcile two CXXRecordDecls that agree on layout and vtable *shape*
// but fundamentally disagree on whether the class is abstract. Depending
// on import order, the scratch AST's single merged 'Shared' RecordDecl can
// end up with a DefinitionData whose "abstract" bit reflects one dylib's
// definition while the actual CXXMethodDecl for 'go' (and its associated
// vtable info) reflects the other's -- an internally inconsistent state
// that a normal Sema-driven parse could never produce.
struct Shared {
  virtual void go() {}
  virtual ~Shared() {}
};

struct ConcreteTwo : Shared {
  void go() override {}
};

ConcreteTwo *gConcreteTwo = nullptr;
Shared *gSharedTwo = nullptr;

extern "C" {

void dylib_two_init() {
  gConcreteTwo = new ConcreteTwo;
  gSharedTwo = gConcreteTwo;
}

} // extern "C"

#include "plugin.h"
#include <memory>

// This is the exe's definition of 'Base': it has a virtual destructor and
// a virtual method, so it is a polymorphic class with a vtable pointer.
// Compare this to the dylib's same-named 'Base' class (see plugin.cpp),
// which has neither a virtual destructor nor a virtual method, and
// therefore has no vtable pointer at all. This is a deliberate ODR
// violation: the two CXXRecordDecls for 'Base' disagree not just on
// layout details but on whether the class is a dynamic (polymorphic)
// class in the first place. That changes what clang::ASTRecordLayout
// computes for the type's size (an extra 8-byte vtable pointer field)
// and whether a synthesized vtable-pointer FieldDecl/VarDecl needs to
// exist at all.
struct Base {
  virtual ~Base() {}
  virtual void f() {}
  int x = 1;
};

int main() {
  // Keep a polymorphic 'Base' instance alive through a std::unique_ptr,
  // and delete it polymorphically (via the base class' virtual
  // destructor) once we are done. Referencing 'Base' through
  // std::unique_ptr<Base> pulls in a bunch of libc++ template
  // instantiations (default_delete<Base>, unique_ptr<Base>::~unique_ptr,
  // etc.) that all reference this exe's polymorphic definition of 'Base',
  // which is exactly the kind of expression evaluation that ends up
  // importing this 'Base' into LLDB's shared scratch AST context.
  std::unique_ptr<Base> b(new Base());

  plugin_init();
  plugin_entry();

  b->f();
  return 0;
}

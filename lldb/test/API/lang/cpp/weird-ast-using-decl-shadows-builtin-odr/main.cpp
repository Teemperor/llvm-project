#include "plugin.h"

// The main executable defines 'mynum::BigInt' as a single 'long' field,
// together with a free 'operator+' that returns 'BigInt' by value. Unlike
// the sibling 'weird-ast-operator-overload-odr' test (which uses ordinary
// qualified names), this test additionally pulls the operator and the
// struct into the *global* namespace via using-declarations:
//
//   using mynum::operator+;
//   using mynum::BigInt;
//
// 'using mynum::operator+;' is a UsingDecl that names an overloaded
// operator: Sema resolves and shadows it via a dedicated
// UsingShadowDecl/CheckUsingDeclRedeclaration path that is normally never
// asked to reconcile two *different* incompatible 'operator+' overloads
// (one returning 'BigInt' by value / sret, the other returning 'double' in
// a scalar register -- see plugin.cpp) being merged into the same global
// scope of the shared per-target scratch AST context.
namespace mynum {
struct BigInt {
  long v;
};
BigInt operator+(BigInt a, BigInt b) { return {a.v + b.v}; }
} // namespace mynum

using mynum::operator+;
using mynum::BigInt;

mynum::BigInt xa{5};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

// Tests evaluating an expression while stopped in a member function whose class
// is only *declared* in the debug info.
//
// With limited debug info (the `-fno-standalone-debug` default on non-Darwin,
// and whenever a class's key function is emitted elsewhere) clang can emit the
// body of an inline member function into a translation unit that only carries a
// `DW_AT_declaration` DIE for the enclosing class -- here `Cls`, whose key
// function `~Cls()` lives in a translation unit built without debug info, so no
// module holds a definition of `Cls` at all.
//
// The expression wrapper must not be emitted as a member function of that class
// then: there is no class definition to inject `$__lldb_expr` into. TypeSystemClike
// used to unconditionally add the method to the (definition-less) CXXRecordDecl,
// which tripped clang's "queried property of class with no definition" assertion
// in CXXRecordDecl::data(). A plain function wrapper is used instead, matching
// what TypeSystemClang does for the same debug info: an expression that does not
// need `this` evaluates fine, and an unqualified member reference is simply an
// undeclared identifier.

// UNSUPPORTED: system-windows

// RUN: %clangxx_host -g0 -O0 -DKEY_TU -c %s -o %t-key.o
// RUN: %clangxx_host -g -O0 -fno-standalone-debug -c %s -o %t-main.o
// RUN: %clangxx_host %t-key.o %t-main.o -o %t.out
// RUN: %lldb %t.out -o "b Cls::getVal" -o run -o "expr 1" -o "expr val" \
// RUN:   -o exit 2>&1 | FileCheck %s

struct Cls {
  virtual ~Cls();
  int val;
  int getVal() { return val + 1; }
};

#ifdef KEY_TU
Cls::~Cls() {}
Cls *makeCls() {
  Cls *c = new Cls();
  c->val = 41;
  return c;
}
#else
Cls *makeCls();

int main() {
  Cls *p = makeCls();
  return p->getVal();
}
#endif

// The frame is a member function of a class with no definition anywhere.
// CHECK: stop reason = breakpoint
// CHECK: Cls::getVal

// An expression that does not touch the object still evaluates.
// CHECK: (lldb) expr 1
// CHECK: (int) $0 = 1

// An unqualified member reference cannot resolve (there is no class definition
// to look it up in), but it must be a normal diagnostic.
// CHECK: (lldb) expr val
// CHECK: error: use of undeclared identifier 'val'

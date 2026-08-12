#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template called 'Outer' whose nested (but
// non-template) member class 'Inner' has different field layouts. Both
// this file and plugin.cpp instantiate 'Outer<int>', so the nested class
// 'Outer<int>::Inner' -- a plain CXXRecordDecl whose DeclContext is the
// ClassTemplateSpecializationDecl 'Outer<int>', not a template itself --
// ends up with two incompatible definitions once both get imported into
// the same per-target scratch AST context. This is a genuine ODR
// violation: unlike using different template arguments (which naturally
// produces distinct types), here the exact same specialization
// 'Outer<int>' (and its nested 'Inner') has two incompatible bodies.
template <typename T> struct Outer {
  struct Inner {
    T val;
    int a;
  };
  Inner slot;
};

Outer<int> main_outer = {{1, 2}};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

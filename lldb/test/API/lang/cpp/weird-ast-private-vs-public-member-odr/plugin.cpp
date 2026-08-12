#include "plugin.h"

// See main.cpp: this is the dylib's conflicting definition of 'Secret'.
// Same field name ('value'), same type ('int'), same offset (0) as
// main.cpp's 'Secret', but 'value' is *private* here instead of public.
// A constructor is needed because 'value' being private means this class
// is no longer an aggregate, so it can't be brace-initialized from outside
// like main.cpp's 'Secret' is.
class Secret {
  int value;

public:
  Secret(int v) : value(v) {}
  int getValue() { return value; }
};

Secret secretFromPlugin = {99};

// A second, related ODR conflict: 'Base' is inherited *publicly* by
// main.cpp's 'Derived' (see below) but *privately* here. Unlike a data
// member's AccessSpecifier (which LLDB's DWARF parser ignores completely
// -- see TestWeirdAstPrivateVsPublicMemberOdr.py), a base class's
// accessibility *is* read from DW_AT_accessibility by
// DWARFASTParserClang::ParseInheritance() and preserved on the imported
// CXXBaseSpecifier. So this is the one place where LLDB's Clang AST can
// end up with a real, externally-visible AccessSpecifier ODR conflict
// between two same-named types.
struct Base {
  virtual ~Base() {}
  int value;
};

class Derived : private Base {
public:
  Derived(int v) { value = v; }
  virtual int getValue() { return value; }
};

Derived derivedFromPlugin(199);

extern "C" {
void plugin_init() {}

void plugin_entry(void *secretFromMainExePtr) {
  // Reference both modules' conflicting 'Secret' definitions from the
  // same frame, and force both 'Derived'/'Base' hierarchies to be live
  // too, so an expression evaluated here can pull all of them into the
  // shared scratch AST context together.
  Secret *secretFromMainExe = (Secret *)secretFromMainExePtr;
  int v = secretFromPlugin.getValue();
  int d = derivedFromPlugin.getValue();
  (void)v;
  (void)d;
  (void)secretFromMainExe;
}
}

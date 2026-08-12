#include "plugin.h"

// See main.cpp: this dylib defines same-named 'B1', 'B2' and 'Diamond'
// classes, but here 'Diamond' inherits from B1 and B2 NON-virtually. That
// means each base has its own distinct sub-object (no shared/virtual base),
// and 'Diamond' doesn't need the extra vbase-pointer bookkeeping the exe's
// virtual-inheritance version requires. This is about as structurally
// different as two same-named CXXRecordDecls can get: merging them forces
// LLDB's ASTImporter/TypeSystemClang to reconcile completely incompatible
// base-class specifiers (virtual vs. non-virtual) for the same class name.
class B1 {
public:
  int b1Field = 10;
};

class B2 {
public:
  int b2Field = 20;
};

class Diamond : public B1, public B2 {
public:
  int diamondField = 30;
};

Diamond *gPluginDiamond = nullptr;

extern "C" {
void plugin_init() { gPluginDiamond = new Diamond(); }

void plugin_entry() {}
}

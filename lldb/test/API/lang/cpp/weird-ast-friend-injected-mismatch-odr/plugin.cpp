#include "plugin.h"

// See main.cpp: this dylib defines a same-named 'class Box' with a
// completely different, incompatible layout (a 'double secret' plus a
// 'long pad', instead of the exe's single private 'int secret'), and its
// own friend-injected free function 'peek' with the identical
// name/mangling pattern ('peek(Box&)') operating on that incompatible
// layout.
//
// This is an ODR violation: two definitions of 'class Box' exist across
// the exe and dylib, each with its own FriendDecl-injected 'peek' free
// function threaded into the translation-unit DeclContext, but the two
// 'Box' types are not layout-compatible at all. When LLDB's
// ASTImporter/TypeSystemClang/DWARFASTParserClang machinery imports and
// merges these conflicting CXXRecordDecls (and their friend-injected
// 'peek' FunctionDecls) into the shared per-target scratch AST context,
// the resulting bookkeeping for the friend/lookup tables may end up
// inconsistent.
class Box {
  double secret = 2.0;
  long pad = 7;
  friend int peek(Box &);
};

int peek(Box &b) { return (int)b.secret; }

Box plugin_box;

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Force emission/use of peek(Box&) so debug info for Box and its
  // FriendDecl-injected friend function definitely exists in the dylib.
  int v = peek(plugin_box);
  v += 0;
}
}

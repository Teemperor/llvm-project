#include "plugin.h"

// Primary class template. main.cpp only ever uses this primary template:
// it never sees an explicit specialization for 'Holder<int>', so
// 'Holder<int>' here is instantiated implicitly from the primary template
// the first time it is used below (for 'main_holder').
//
// See plugin.cpp for the crucial twist: the dylib provides an *explicit
// full specialization* of 'Holder<int>' with a different body, defined
// and used *before* the main executable's implicit instantiation is ever
// imported into LLDB's scratch AST context.
//
// This means the exact same type-id 'Holder<int>' is represented very
// differently inside Clang's AST depending on which module's debug info
// LLDB parses first:
//   - main executable: a ClassTemplateSpecializationDecl with
//     TemplateSpecializationKind TSK_ImplicitInstantiation, body copied
//     from the primary template ('T v;').
//   - dylib: a ClassTemplateSpecializationDecl with
//     TemplateSpecializationKind TSK_ExplicitSpecialization, with its own
//     independently-written body ('int v; int extra; long tag;').
//
// Reconciling one module's implicit-instantiation node for 'Holder<int>'
// with the other module's explicit-specialization node for the very same
// template-id is a mismatch the ASTImporter's ODR/structural-equivalence
// machinery may not expect at all - it is used to dealing with two
// *implicit* instantiations disagreeing, not with one side being an
// explicit specialization in the first place.
template <typename T> struct Holder {
  T v;
};

// Implicitly instantiates 'Holder<int>' from the primary template above.
Holder<int> main_holder = {11};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

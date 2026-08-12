#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template called 'Wrapper' and an alias
// template 'Ptr' that both spell 'Wrapper<T> *', but the two modules'
// 'Wrapper<int>' bodies differ (see plugin.cpp for the conflicting
// version). This is a genuine ODR violation on the same specialization
// 'Wrapper<int>', wrapped behind identical alias-template sugar in both
// modules.
//
// Alias templates like 'Ptr' are pure sugar: DWARF may or may not
// preserve them, so when LLDB's expression parser sees 'Ptr<int>' it has
// to independently re-derive the underlying type ('Wrapper<int> *') via
// TypeSystemClang, while the ASTImporter is (elsewhere) busy importing a
// conflicting 'Wrapper<int>' RecordDecl from DWARF for the other module.
// The hope is that the alias-substituted 'Wrapper<int>' and the
// DWARF-imported 'Wrapper<int>' end up as two different, incompatible
// QualTypes colliding inside the same expression.
template <typename T> struct Wrapper {
  T v;
  long extra;
};
template <typename T> using Ptr = Wrapper<T> *;

Wrapper<int> g_main_wrapper = {111, 222};
Ptr<int> main_ptr = &g_main_wrapper;

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

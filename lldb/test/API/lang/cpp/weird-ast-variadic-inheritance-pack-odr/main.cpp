#include "plugin.h"

// 'Mixin' is a simple one-field template: instantiating it for a type 'T'
// gives a base class that only contributes a single 'tag' member.
template <typename T> struct Mixin {
  T tag;
};

// 'Multi' inherits from 'Mixin<Ts>...' for each type in the pack Ts, via a
// pack expansion in the base-specifier list (multiple inheritance
// generated entirely from a template parameter pack, rather than a
// hand-written base list). Completing 'Multi<...>' therefore requires
// Clang to expand the TemplateArgument pack into a variable-length
// CXXBaseSpecifier array attached to the CXXRecordDecl, and to lay out
// each Mixin<T> sub-object in pack order.
template <typename... Ts> struct Multi : Mixin<Ts>... {
  int id;
};

// This translation unit's alias 'M' names 'Multi<int, char>': base order
// is Mixin<int>, then Mixin<char>.
//
// Note: deliberately not shared via a common header. plugin.cpp defines
// the exact same alias name 'M' via the exact same template 'Multi', but
// instantiates the pack in the OPPOSITE order ('Multi<char, int>'). Same
// base-class *set*, different base-class *order* (and therefore
// potentially different layout/offsets) for what LLDB's DWARF parser
// reports as "the same" typedef'd type name across the two modules. This
// is a genuine ODR violation.
using M = Multi<int, char>;

M global_m;

int main() {
  global_m.id = 1;
  global_m.Mixin<int>::tag = 2;
  global_m.Mixin<char>::tag = 'A';

  plugin_init();
  plugin_entry();
  return 0;
}

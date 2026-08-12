#include "plugin.h"

// This 'Mixin' template is byte-for-byte identical to main.cpp's, but see
// below: the *order* in which this dylib instantiates the variadic
// 'Multi' pack differs from main.cpp's instantiation order for the exact
// same alias name 'M'.
template <typename T> struct Mixin {
  T tag;
};

// Same variadic class template as main.cpp: 'Multi' inherits from
// 'Mixin<Ts>...' for each type in the pack, via a pack expansion in the
// base-specifier list. Multiple inheritance here comes entirely from
// expanding the template parameter pack, not from a hand-written base
// list, so completing/laying out 'Multi<...>' exercises Clang's
// TemplateArgument-pack-to-CXXBaseSpecifier expansion machinery.
template <typename... Ts> struct Multi : Mixin<Ts>... {
  int id;
};

// Deliberately different pack order than main.cpp's 'M': main.cpp defines
// 'using M = Multi<int, char>;' (base order Mixin<int>, then Mixin<char>),
// while this dylib defines the SAME alias name 'M' as
// 'Multi<char, int>' (base order Mixin<char>, then Mixin<int>). Both
// modules' 'M' therefore have the same *set* of base classes but a
// different *order*, which can affect the base-class layout (and
// therefore the offset of 'id' and of each Mixin<T>::tag) even though
// both are DWARF-visible under the identical typedef name 'M'. This is a
// genuine ODR violation across the executable and the dylib.
using M = Multi<char, int>;

M *gPluginM = nullptr;

extern "C" {
void plugin_init() {
  gPluginM = new M();
  gPluginM->id = 10;
  gPluginM->Mixin<char>::tag = 'B';
  gPluginM->Mixin<int>::tag = 30;
}

void plugin_entry() {}
}

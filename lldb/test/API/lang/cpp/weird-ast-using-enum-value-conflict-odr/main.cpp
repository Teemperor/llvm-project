#include "plugin.h"

// This file and plugin.cpp both declare a namespace-scoped scoped-enum
// called 'colors::Color' and pull it into the global namespace via a
// using-declaration. This is a genuine ODR violation: the two definitions
// have different underlying types (this one implicitly 'int', plugin.cpp's
// is explicitly 'char') and a different number of enumerators (this one has
// 3, plugin.cpp's has 4, with the extra enumerator 'Alpha' only existing in
// plugin.cpp's definition).
//
// Because both definitions share the same qualified name 'colors::Color',
// LLDB's ASTImporter has to reconcile them when both get imported into the
// shared per-target scratch AST context, which can end up with 'Color'
// (found via the using-declaration) resolving to whichever definition was
// imported first -- even if that's not the definition relevant to the
// enumerator being referenced.
namespace colors {
enum class Color { Red, Green, Blue };
} // namespace colors
using colors::Color;

Color cA = Color::Red;

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

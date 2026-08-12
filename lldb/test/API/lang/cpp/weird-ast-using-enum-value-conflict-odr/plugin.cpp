#include "plugin.h"

// See main.cpp: this 'colors::Color' has the same qualified name as the one
// in main.cpp, but is explicitly backed by 'char' (instead of the implicit
// 'int') and has an extra enumerator ('Alpha') that doesn't exist in
// main.cpp's definition.
namespace colors {
enum class Color : char { Red, Green, Blue, Alpha };
} // namespace colors
using colors::Color;

Color cB = Color::Alpha;

extern "C" {
void plugin_init(void) {}

void plugin_entry(void) {}
}

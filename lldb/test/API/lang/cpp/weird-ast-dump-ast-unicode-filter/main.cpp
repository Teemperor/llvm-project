#include "plugin.h"

// Fifty levels of nested class template instantiation:
// Wrap<Wrap<Wrap<...<int>...>>> (50 nestings of 'Wrap<...>'). Clang's AST
// pretty-printer/traversal (used by 'target modules dump ast' and by its
// '--filter' name-matching logic) has to recurse once per
// template-argument level to print or walk this type's fully qualified
// name. See plugin.cpp for the independently-defined, structurally
// identical copy used by the dylib side.
template <typename T> struct Wrap { T val; };

using DeepType =
    Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap
    <Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap
    <Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap
    <Wrap<Wrap<Wrap<Wrap<Wrap<int>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;

DeepType g_deep_from_main;

void main_entry() {}

int main() {
  plugin_init(&g_deep_from_main);
  plugin_entry();
  main_entry();
  return 0;
}

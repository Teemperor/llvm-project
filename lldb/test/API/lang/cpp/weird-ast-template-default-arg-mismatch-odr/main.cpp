#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template called 'Grid' with a default value
// for its second (non-type) template parameter, but the *default* differs
// between the two definitions (4 here, 8 in plugin.cpp). Both files then
// instantiate the template via the bare spelling 'Grid<int>', which really
// means 'Grid<int, 4>' here and 'Grid<int, 8>' in plugin.cpp.
//
// This is built with -gsimple-template-names (see Makefile), which makes
// clang emit the DW_TAG_structure_type DIE's DW_AT_name as the bare
// "Grid" (no template arguments at all) in both object files -- the
// actual arguments only show up as DW_TAG_template_type_parameter /
// DW_TAG_template_value_parameter children. This means
// DWARFASTParserClang has to reconstruct the specialization's template
// arguments from those child DIEs rather than from the printed name, so if
// it ever took a shortcut and keyed a specialization lookup/cache off of
// the bare textual name "Grid" alone, it could conflate the two
// mutually-incompatible 'Grid<int, 4>' and 'Grid<int, 8>' specializations
// coming from the two different modules.
template <typename T, int N = 4> struct Grid {
  T cells[N];
};

Grid<int> main_g = {{1, 2, 3, 4}};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

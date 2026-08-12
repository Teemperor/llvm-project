#include "plugin.h"

// This definition of 'Table' is only visible to plugin.cpp. It reuses
// the same struct/member names as main.cpp's 'Table', but its static
// data member 'kSize' has the value 64 instead of 4. Since 'data's
// array bound is 'kSize', the dylib's notion of 'Table' has a member
// array (and therefore an overall struct size) that is drastically
// different from main.cpp's notion of the (identically spelled)
// 'Table' type, even though both declarations look the same at the
// AST/DWARF level ("static const int kSize = <N>; int data[kSize];").
struct Table {
  static const int kSize = 64;
  int data[kSize];
  int tag;
};

Table gPluginTable = {{}, 2};

extern "C" {
void plugin_init() {
  for (int i = 0; i < Table::kSize; ++i)
    gPluginTable.data[i] = i;
}

void plugin_entry() {}
}

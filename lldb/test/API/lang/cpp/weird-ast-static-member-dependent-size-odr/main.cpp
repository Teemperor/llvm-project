#include "plugin.h"

// This definition of 'Table' is only visible to main.cpp. Its static
// data member 'kSize' has the value 4, which determines the bound of
// the following array member 'data'. This makes the overall byte size
// of 'Table' (and the type of 'data') depend on the *value* of a
// static const int member rather than just its spelling.
struct Table {
  static const int kSize = 4;
  int data[kSize];
  int tag;
};

Table gMainTable = {{10, 20, 30, 40}, 1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

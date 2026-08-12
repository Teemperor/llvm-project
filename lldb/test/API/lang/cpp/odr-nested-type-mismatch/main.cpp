#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define 'Outer::Inner', but with different members. This is an
// ODR violation involving a nested class, which the ASTImporter has to
// import through the enclosing 'Outer' context.
struct Outer {
  struct Inner {
    int x;
  };
};

Outer::Inner global_inner = {1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

#include "plugin.h"

// See main.cpp: this 'Helper' has the exact same name and members as the
// main executable's 'Helper', but is declared at ordinary namespace scope,
// giving it *external* linkage (unlike main.cpp's anonymous-namespace,
// internal-linkage 'Helper').
struct Helper {
  int tag;
  void run();
};

void Helper::run() { tag = 200; }

Helper gPluginHelper = {2};

extern "C" {
void plugin_init() { gPluginHelper.run(); }

void plugin_entry() {}
}

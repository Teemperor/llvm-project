#include "plugin.h"

// See main.cpp: this 'Wrapper<int>' has the same name and template
// argument as the one in main.cpp, and the alias template 'Ptr' has the
// identical spelling ('Wrapper<T> *'), but this module's 'Wrapper<int>'
// body drops 'extra' and instead adds an unrelated 'short tag' member -
// a genuine ODR violation on the same specialization.
template <typename T> struct Wrapper {
  T v;
  short tag;
};
template <typename T> using Ptr = Wrapper<T> *;

Wrapper<int> g_plugin_wrapper = {333, 7};
Ptr<int> plugin_ptr = &g_plugin_wrapper;

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Break here. At this point neither module's 'Wrapper<int>'/'Ptr<int>'
  // has been imported into the target's shared scratch AST context yet.
  int x = plugin_ptr->v;
  (void)x;
}
}

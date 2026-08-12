#include "plugin.h"

// This is the "top-level" libfoo.dylib: it gets linked directly into the
// main executable in the usual way, and is therefore already loaded by
// the time main() starts running. Its 'Widget' has a single 'int' member.
//
// A second, differently-built copy of "libfoo.dylib" (see foo_hidden.cpp)
// is loaded later on via dlopen() with a full path pointing at a
// different directory, but claims the exact same LC_ID_DYLIB install
// name as this one -- see main.cpp for the load-time trick.
class Widget {
public:
  int x;
  Widget(int v) : x(v) {}
};

static Widget *gTopWidget = nullptr;

extern "C" {

void *libfoo_top_init() {
  static Widget w(111);
  gTopWidget = &w;
  return gTopWidget;
}

} // extern "C"

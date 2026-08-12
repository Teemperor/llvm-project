// This is the "hidden" libfoo.dylib: it is built from different source
// than the top-level libfoo.dylib (see foo_top.cpp), and the Makefile
// places the built binary in a *different* directory ("hidden/") than the
// top-level one. Despite that, both are given the exact same LC_ID_DYLIB
// install name (see the Makefile), and main.cpp loads this copy via
// dlopen() with an explicit full path, bypassing the usual load-time
// dedup-by-install-name that dyld would otherwise apply.
//
// This dylib's 'Widget' is spelled identically to the top-level dylib's
// (same class name, same tag kind), but has a completely different,
// incompatible layout: two 'long' members instead of a single 'int'
// member.
class Widget {
public:
  long x;
  long y;
  Widget(long v) : x(v), y(v * 2) {}
};

static Widget *gHiddenWidgetLocal = nullptr;

extern "C" {

void *libfoo_hidden_init() {
  static Widget w(222);
  gHiddenWidgetLocal = &w;
  return gHiddenWidgetLocal;
}

} // extern "C"

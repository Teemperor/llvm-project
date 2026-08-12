#include "plugin.h"

// Doubled CRTP diamond: 'Widget' has TWO independent CRTP bases,
// 'Counted<Widget>' and 'Named<Widget>', each parameterized on 'Widget'
// itself. Unlike weird-ast-crtp-self-base-odr (a single CRTP base), here
// *two* class template specializations are simultaneously cyclic through
// the same derived RecordDecl: importing/completing 'Widget' requires
// resolving 'Counted<Widget>' AND 'Named<Widget>', each of which names
// 'Widget' again via its own template argument.
//
// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define identical 'Counted<D>'/'Named<D>' templates and an
// identical 'Widget' (same bases, same 'extra' field), but plugin.cpp's
// 'Widget' inherits from the two CRTP bases in the OPPOSITE order. Same
// base classes, same derived-class field, but different base sub-object
// byte offsets -- an ODR violation across LLDB modules (the executable and
// the dylib) that is layout-only, not shape-only.
template <typename D> struct Counted {
  static int count;
  int id;
};
template <typename D> int Counted<D>::count = 0;

template <typename D> struct Named {
  const char *name;
};

struct Widget : Counted<Widget>, Named<Widget> {
  int extra;
};

Widget main_widget;

int main() {
  main_widget.id = 1;
  main_widget.name = "main";
  main_widget.extra = 100;
  Counted<Widget>::count = 42;

  plugin_init();
  plugin_entry();
  return 0;
}

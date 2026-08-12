#include "plugin.h"

// See main.cpp: identical 'Counted<D>'/'Named<D>' CRTP templates, but this
// dylib's 'Widget' inherits from them in the OPPOSITE order ('Named'
// first, 'Counted' second). Same set of bases and the same 'extra' field
// as main.cpp's 'Widget', but flipping the inheritance order changes each
// base's byte offset within the derived object -- 'Counted<Widget>' and
// 'Named<Widget>' swap places in the layout compared to main.cpp, while
// still both being CRTP-cyclic through the same 'Widget' name.
template <typename D> struct Counted {
  static int count;
  int id;
};
template <typename D> int Counted<D>::count = 0;

template <typename D> struct Named {
  const char *name;
};

struct Widget : Named<Widget>, Counted<Widget> {
  int extra;
};

Widget plugin_widget;

extern "C" {
void plugin_init() {
  plugin_widget.id = 2;
  plugin_widget.name = "plugin";
  plugin_widget.extra = 200;
  Counted<Widget>::count = 84;
}

void plugin_entry() {}
}

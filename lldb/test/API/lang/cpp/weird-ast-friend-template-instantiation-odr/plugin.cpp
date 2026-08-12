#include "plugin.h"

// See main.cpp: this 'Holder<int>' has the same name/template argument as
// the one in main.cpp, but its body has an extra member ('v2'), and this
// 'Peeker::get' has a different return type ('double' instead of 'int').
// 'Peeker' is a friend of 'Holder<int>' in both translation units, but the
// two 'Holder<int>' specializations are not layout-compatible and the two
// 'Peeker::get' overloads are not signature-compatible: a genuine ODR
// violation.
template <typename T> class Holder {
  T v;
  T v2;
  friend class Peeker;
};

class Peeker {
public:
  static double get(Holder<int> &h);
};

double Peeker::get(Holder<int> &h) { return 2.0; }

Holder<int> hb;

extern "C" {
void plugin_init(void) {}
void plugin_entry(void) {}
}

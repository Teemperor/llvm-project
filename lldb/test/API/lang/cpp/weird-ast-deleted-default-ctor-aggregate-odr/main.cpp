#include "plugin.h"

// This definition of 'Pod' is only visible to main.cpp. It has an explicit
// user-declared default constructor that is deleted, so 'Pod' is no longer
// an aggregate in the C++ sense (a user-declared constructor -- even a
// deleted one -- disqualifies a class from being an aggregate). This is
// still legal, well-formed C++: as long as nothing ever tries to
// default-construct a 'Pod', the deleted default constructor is never
// actually needed, and 'gMainPod' below can still be initialized using
// list-initialization syntax with explicit values for both members (that
// does not go through the deleted default constructor at all).
struct Pod {
  int a;
  int b;
  Pod() = delete;
};

Pod gMainPod{1, 2};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

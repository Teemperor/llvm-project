#include "plugin.h"
#include <type_traits>

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template called 'Choice' instantiated with the
// exact same template argument (int). The data layout is identical in both
// definitions (a single 'int data' member), so this is *not* the already
// covered "same specialization, different body/layout" ODR violation.
// Instead, only the *method set* diverges: 'pick()' is a member function
// template disambiguated via 'std::enable_if' SFINAE on 'sizeof(U)'. Here
// the condition is 'sizeof(U) == 4', so for 'Choice<int>' this overload
// exists and returns 1.
template <typename T> struct Choice {
  template <typename U = T>
  typename std::enable_if<sizeof(U) == 4, int>::type pick() {
    return 1;
  }
  int data;
};

// Force the SFINAE'd member function template to actually be instantiated
// (and thus show up in debug info) for 'Choice<int>'.
Choice<int> main_choice = {42};

int main() {
  int result = main_choice.pick();
  plugin_init();
  plugin_entry();
  return result;
}

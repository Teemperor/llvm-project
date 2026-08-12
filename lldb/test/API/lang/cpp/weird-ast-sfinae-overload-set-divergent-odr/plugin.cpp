#include "plugin.h"
#include <type_traits>

// See main.cpp: this 'Choice<int>' has the same name, same template
// argument, and the same data layout ('int data') as the one in main.cpp.
// The only difference is that the SFINAE condition on the member function
// template 'pick()' is inverted ('sizeof(U) != 4' instead of
// 'sizeof(U) == 4'). For 'Choice<int>' that means this 'pick()' overload
// does not exist at all (it is SFINAE'd away) -- so 'Choice<int>::pick()'
// either exists-with-a-body in main.cpp's definition, or is completely
// absent in plugin.cpp's definition, while the two RecordDecls otherwise
// agree on layout ('data' is a plain 'int' in both).
template <typename T> struct Choice {
  template <typename U = T>
  typename std::enable_if<sizeof(U) != 4, int>::type pick() {
    return 2;
  }
  int data;
};

Choice<int> *gPluginChoice = nullptr;

extern "C" {
void plugin_init() { gPluginChoice = new Choice<int>{99}; }

void plugin_entry() {}
}

#include "plugin.h"

// See main.cpp: this 'Outer<int>' has the same name and template argument
// as the one in main.cpp, but its nested 'Inner' class has its fields
// swapped ('a' comes before 'val').
template <typename T> struct Outer {
  struct Inner {
    int a;
    T val;
  };
  Inner slot;
};

Outer<int> plugin_outer = {{20, 10}};

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}

#include "plugin.h"

// Independently-defined, structurally identical copies of the same
// regex-metacharacter-laden types as main.cpp, so that the dylib's own
// debug info also contains DWARF DIE trees for 'Vec<int>', 'Outer::Inner'
// and 'Op's operator overloads.
template <typename T> struct Vec {
  T data[4];
  T Get(int i) { return data[i]; }
};

struct Outer {
  struct Inner {
    int value = 42;
  };
};

struct Op {
  int x = 2;
  int operator()(int y) { return x + y; }
  int operator[](int i) { return x + i; }
  Op operator+(const Op &rhs) const {
    Op result;
    result.x = x + rhs.x;
    return result;
  }
};

Vec<int> g_vec_from_plugin;
Outer::Inner g_inner_from_plugin;
Op g_op_from_plugin;

extern "C" {
void plugin_init() {
  g_vec_from_plugin.data[0] = 7;
  g_inner_from_plugin.value = 44;
  g_op_from_plugin(5);
  g_op_from_plugin[1];
}

void plugin_entry() {
  // Breakpoint here. By this point plugin_init() above has already run,
  // but none of these types have necessarily been *parsed into LLDB's
  // Clang AST* yet -- that only happens once an expression is evaluated
  // against them.
  int local = 0;
  local += g_vec_from_plugin.data[0];
}
}

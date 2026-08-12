#include "plugin.h"

// This file and plugin.cpp each define a namespace 'geo' containing a
// struct 'Point' and a free function 'translate' found only via
// argument-dependent lookup (ADL) on 'Point' (there is no using-declaration
// or shared header pulling the two definitions together). The two
// 'geo::Point's are a genuine ODR violation: this one has two 'int'
// members, plugin.cpp's has three. 'geo::translate' also differs in
// arity to match: this file's overload takes a single 'dx' offset,
// plugin.cpp's takes both 'dx' and 'dy'.
//
// 'pa' below is only ever touched via this file's 2-field 'Point' and
// 1-offset 'translate'. 'plugin.cpp's 'pb' is only ever touched via its
// own 3-field 'Point' and 2-offset 'translate'. Both globals are alive
// at the same time when the process stops in 'plugin_entry', so
// evaluating expressions that ADL-call 'translate' on 'pa' and then on
// 'pb' (or vice versa) in the same expression-evaluation session forces
// LLDB's ASTImporter/TypeSystemClang to reconcile the two conflicting
// 'geo' namespaces (each with its own incompatible 'Point' and its own
// differently-shaped 'translate' overload) inside the per-target shared
// scratch AST context.
namespace geo {
struct Point {
  int x, y;
};
Point translate(Point p, int dx) {
  p.x += dx;
  return p;
}
} // namespace geo

geo::Point pa{0, 0};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

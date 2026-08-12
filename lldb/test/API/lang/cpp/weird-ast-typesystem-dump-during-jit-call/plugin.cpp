#include "plugin.h"

#include <chrono>
#include <thread>

// A type whose debug info lives in this dylib. The test forces this type
// to be parsed/completed into LLDB's Clang AST machinery (both the
// per-module DWARF-parsed AST and, from there, the per-target shared
// scratch ASTContext) before racing 'target modules dump ast --filter
// Widget' against a blocking JIT'd call to SlowCompute() below.
struct Widget {
  int a = 1;
  double b = 2.0;
  int compute() { return a; }
};

extern "C" {

// Spins for a few seconds -- long enough that a JIT'd call to this
// function (made via LLDB's expression evaluator) keeps the calling
// thread, and LLDB's private state thread, busy resuming/waiting on the
// inferior for a comfortably long window.
int SlowCompute(void) {
  std::this_thread::sleep_for(std::chrono::seconds(3));
  return 42;
}

Widget *plugin_make_widget(void) { return new Widget(); }

void plugin_entry(void) {
  Widget w;
  (void)w;
}

} // extern "C"

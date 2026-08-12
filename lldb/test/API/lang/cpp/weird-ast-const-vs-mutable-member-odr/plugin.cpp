#include "plugin.h"

// Dylib's definition of 'Counter': structurally identical layout to the
// main executable's 'Counter' (a single 'int' field named 'hits'), but here
// 'hits' is a plain, non-mutable field. bump() is a const no-op that never
// touches 'hits', so this compiles even though 'hits' isn't mutable. This is
// a real-world-plausible ODR violation: both TUs saw a header (or just
// happened to write) a type named 'Counter' with the same name/size/layout
// for 'hits', but disagree about whether 'hits' is mutable.
struct Counter {
  int hits;
  void bump() const { /* no-op: doesn't touch hits */ }
};

// A global *const* Counter. Because this TU's 'hits' is not mutable, the
// whole object is a true compile-time constant as far as this TU is
// concerned, and the compiler is free to (and, on Darwin, does) place it in
// read-only memory.
const Counter counterFromDylib = {42};

// See the comment on the analogous declaration in main.cpp: this forces
// emission of *this* TU's Counter::bump() (an identically-mangled but
// differently-behaving function) without ever calling it by name, so dyld
// never has to arbitrate between the two same-named definitions itself.
__attribute__((used)) static void (Counter::*g_forceEmitBumpDylib)() const =
    &Counter::bump;

extern "C" void plugin_init() {}

extern "C" void plugin_entry() {
  // Breakpoint location. At this point both the main executable's and the
  // dylib's conflicting 'Counter' have been seen by LLDB, but neither has
  // necessarily been imported into the per-target scratch AST yet.
  int x = counterFromDylib.hits;
  (void)x;
}

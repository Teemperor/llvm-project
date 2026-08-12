#include "plugin.h"

// Main-executable's definition of 'Counter': 'hits' is genuinely mutable, so
// writing to it through a 'bump() const' method on a 'const Counter' object
// is well defined C++.
struct Counter {
  mutable int hits;
  void bump() const { hits++; }
};

// A global *non-const* Counter so calling bump() on it at startup is always
// safe, regardless of which section the linker decides to place it in.
Counter counterFromMain = {0};

// Force the compiler to emit Counter::bump() (so its symbol exists for LLDB
// to call into) without ever actually calling it by name here. If both TUs
// called an identically-mangled 'Counter::bump()' by name, the dynamic
// linker would have to pick a single winner among the two (weak/external)
// definitions for *all* callers process-wide, which would crash the program
// outright the moment libplugin.dylib's own internal call to bump() from
// plugin_init() got redirected to this incompatible definition -- well
// before LLDB's expression evaluator ever gets a chance to run. Taking the
// address like this keeps both definitions alive as distinct, addressable
// functions that LLDB's expression evaluator can dispatch to individually
// (by address, from each module's own debug info) without dyld ever having
// to arbitrate between them itself.
__attribute__((used)) static void (Counter::*g_forceEmitBumpMain)() const =
    &Counter::bump;

int main() {
  counterFromMain.bump();
  plugin_init();
  plugin_entry();
  return 0;
}

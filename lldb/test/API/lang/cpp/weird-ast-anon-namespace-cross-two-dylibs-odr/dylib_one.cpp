#include "dylib_one.h"

// This dylib defines its own private "Internal" struct inside an unnamed
// namespace. Per the C++ standard an unnamed namespace gives its members
// internal linkage (effectively TU-local / module-local): nothing outside
// this translation unit is supposed to be able to name or conflate this
// "Internal" with any other, differently-defined "Internal" that some
// other translation unit (or in this test, some other dylib) happens to
// also spell inside its own unnamed namespace.
//
// This is the small version: a single int member.
namespace {
struct Internal {
  int a;
};
} // namespace

Internal g_one = {111};

extern "C" {
void dylib_one_init() { g_one.a = 111; }
}

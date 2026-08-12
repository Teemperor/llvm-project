// See the comment at the top of main.cpp for the overall scenario.
//
// This side defines the *same linker symbol* as main.cpp's
// 'void impl::run()' (both mangle to _ZN4impl3runEv), but declares and
// defines it here with a completely different, incompatible C++
// signature: two 'int' parameters and an 'int' return value instead of
// zero parameters and 'void'. We force the identical linker symbol via
// the asm() label and mark this definition __attribute__((weak)) too,
// so the linker is free to pick either module's body to satisfy the
// symbol -- it does not need to (and, per the One Definition Rule,
// should not have to) reconcile the two conflicting declared types.
//
// On Mach-O (Darwin), Clang mangles C++ names into an *extra* leading
// underscore on top of the Itanium '_Z...' mangling, so the asm() label
// needs that extra underscore to alias onto the same symbol as ELF's
// plain '_ZN4impl3runEv'.
#include "plugin.h"

#if defined(__APPLE__)
#define RUN_LINKER_SYMBOL "__ZN4impl3runEv"
#else
#define RUN_LINKER_SYMBOL "_ZN4impl3runEv"
#endif

namespace impl {
__attribute__((weak)) int run(int a, int b) asm(RUN_LINKER_SYMBOL);
__attribute__((weak)) int run(int a, int b) { return a + b; }
} // namespace impl

int gPluginResult = 0;

extern "C" {
void plugin_init() {}

void plugin_entry() { gPluginResult = impl::run(1, 2); }
}

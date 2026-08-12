#ifndef WEIRD_AST_SIX_MODULE_RANDOM_PERMUTATION_ODR_PLUGIN_H
#define WEIRD_AST_SIX_MODULE_RANDOM_PERMUTATION_ODR_PLUGIN_H

// Each of the six dylibs (M1 .. M6) exposes exactly one factory function
// that hands back a pointer to its own file-local 'struct Common' global,
// without the main executable (or any other dylib) ever seeing that dylib's
// particular definition of 'Common' at compile time -- main only forward
// declares 'struct Common' itself. All six conflicting definitions only get
// pulled into LLDB's shared per-target scratch AST context when the test's
// expression(s) run.
extern "C" {
struct Common;
Common *MakeCommon1();
Common *MakeCommon2();
Common *MakeCommon3();
Common *MakeCommon4();
Common *MakeCommon5();
Common *MakeCommon6();
}

#endif // WEIRD_AST_SIX_MODULE_RANDOM_PERMUTATION_ODR_PLUGIN_H

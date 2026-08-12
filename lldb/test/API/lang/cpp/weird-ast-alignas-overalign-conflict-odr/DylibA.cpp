#include "DylibA.h"

// Cache-line aligned 'Aligned': alignas(64) forces both alignment and
// (due to padding) sizeof to become 64, even though the struct only has a
// single 'int' member. This is the exact same type name as the plain,
// naturally-aligned 'Aligned' defined independently in DylibB.cpp (with a
// completely different field list), and as the redeclaration the main
// executable itself uses for its global array (see main.cpp).
struct alignas(64) Aligned {
  int x;
};

// A small array so that pointer-subtraction between elements exercises
// ASTContext::getTypeSizeInChars() on this (64-byte, one-field) layout of
// 'Aligned'.
Aligned g_dylibA_aligned[4] = {{1}, {2}, {3}, {4}};

extern "C" {
void dylibA_init(void) {}
}

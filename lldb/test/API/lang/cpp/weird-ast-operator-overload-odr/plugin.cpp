#include "plugin.h"

// See main.cpp: this 'struct Cplx' has the same name as the main
// executable's, but an incompatible layout (an extra 'unused' field, and
// 'float' members instead of 'int'). Its 'operator+' matches this
// dylib's 'Cplx' by component-wise float addition.
struct Cplx {
  float re, im, unused;
};

Cplx operator+(Cplx a, Cplx b) {
  return {a.re + b.re, a.im + b.im, 0};
}

Cplx cb{3, 4, 0};

extern "C" {
void plugin_init() {}

void plugin_entry() {
  // Stop here. At this point 'cb' (this dylib's 'Cplx') is visible in the
  // current frame, and 'ca' (the main executable's incompatible 'Cplx')
  // is visible one frame up, in a_caller(). Evaluating expressions that
  // reference 'ca' and/or 'cb' from here forces LLDB to import both
  // conflicting definitions of 'Cplx' -- and both overloads of
  // 'operator+' -- into the shared per-target scratch AST context.
  Cplx r = cb + cb;
  (void)r;
}
}

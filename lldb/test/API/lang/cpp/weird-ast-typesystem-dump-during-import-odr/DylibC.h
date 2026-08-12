#ifndef DYLIB_C_H_IN
#define DYLIB_C_H_IN

// 'struct Shared' here has three fields: 'a', 'b' and 'c'. This is a
// superset of both the main executable's one-field version ('a' only)
// and DylibB's two-field version ('a', 'b') above. All three definitions
// share the same tag name ('Shared') and agree on the common prefix of
// fields, but disagree on how many fields the type actually has -- a
// three-way ODR violation.
struct Shared {
  int a;
  int b;
  int c;
};

extern "C" {
void dylib_c_entry(void *p);
}

#endif // DYLIB_C_H_IN

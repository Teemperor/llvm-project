#ifndef DYLIB_B_H_IN
#define DYLIB_B_H_IN

// 'struct Shared' here has two fields: 'a' and 'b'. This is a superset of
// the main executable's one-field version ('a' only) and a subset of
// DylibC's three-field version ('a', 'b', 'c') below. All three
// definitions share the same tag name ('Shared') and agree on the common
// prefix of fields, but disagree on how many fields the type actually
// has -- a three-way ODR violation.
struct Shared {
  int a;
  int b;
};

extern "C" {
void dylib_b_entry(void *p);
}

#endif // DYLIB_B_H_IN

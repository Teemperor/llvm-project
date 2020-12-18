// RUN: %clang_cc1 %s -fobjc-runtime=macosx-fragile-10.5 -fsyntax-only -verify=signed
// RUN: %clang_cc1 %s -DBOOL_IS_BOOL -fobjc-runtime=macosx-fragile-10.5 -fsyntax-only -verify=bool

#ifdef BOOL_IS_BOOL
// If BOOL is _Bool then bitfields with a width of 1 are fine.
typedef _Bool BOOL;
#else
typedef signed char BOOL;
#endif

typedef BOOL BOOL_TYPEDEF;

@interface X {
  // If BOOL is a signed char then a 1-bit field isn't wide enough to store
  // YES(1).
  BOOL f : 1;         // signed-warning {{BOOL bit-field 'f' with width 1 can not store BOOL's YES value.}}
  BOOL_TYPEDEF g : 1; // signed-warning {{BOOL bit-field 'g' with width 1 can not store BOOL's YES value.}}
  // Only warn if Objective-C's BOOL type or a typedef for it is used.
  signed char h : 1;
  // A width of 2 or more can store YES and NO.
  BOOL i : 2; // bool-error {{exceeds width of its type}}
  BOOL j : 3; // bool-error {{exceeds width of its type}}
}
@end

struct S {
  BOOL f : 1; // signed-warning {{BOOL bit-field 'f' with width 1 can not store BOOL's YES value.}}
};

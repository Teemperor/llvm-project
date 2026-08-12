#include "plugin.h"

// This is a DIFFERENT (ODR-violating) definition of 'Point3' compared to
// the one in main.cpp: same name and same *number* of data members, but
// here the third member 'z' is a 'long' instead of an 'int'. This changes
// both the layout (this 'Point3' is larger, and 'z' sits at a different
// offset once alignment padding is taken into account) and what the
// compiler-synthesized 'operator==' body actually does: it still compares
// x, then y, then z in declaration order, but the comparison of 'z' reads
// 8 bytes here instead of 4.
//
// The intent is to see whether combining both conflicting definitions in
// a single LLDB expression forces Clang's Sema to reconcile two
// CXXRecordDecls for 'Point3' whose synthesized 'operator==' bodies
// disagree about the type (and therefore the storage size) of the last
// field compared -- and whether that can be pushed into a state where
// IRGen/the IR interpreter reads past the end of a smaller 'Point3'
// object using the wrong (larger) field type.
struct Point3 {
  int x, y;
  long z;
  friend bool operator==(const Point3 &, const Point3 &) = default;
};

// Global so debug-info for this (conflicting) definition of 'Point3' is
// also emitted with a full definition, in the dylib's compile unit.
Point3 point2{1, 2, 3};

// Force the compiler to actually instantiate/emit 'operator==' for this
// TU's 'Point3' too, so DWARF describes a real defined function (see the
// comment on 'force_use_exe' in main.cpp for why this is necessary).
volatile bool force_use_dylib = (point2 == point2);

extern "C" {
void plugin_init() {}

void plugin_entry() {}
}

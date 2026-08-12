// Defines TYPES_H (the guard macro used by types.h) *before* including
// types.h, so the #include below is a no-op due to the (now already
// defined) TYPES_H guard. This header then manually redeclares 'Point'
// itself under its own, different include guard (TYPES_V2_H), with a
// conflicting body (long fields instead of int fields).
//
// The intent is to reach a state where a single translation unit only ever
// sees ONE definition of 'Point' (so each individual .cpp file below is
// itself self-consistent and compiles fine), but the *two* .cpp files in
// this same dylib end up with genuinely different, ODR-violating
// definitions of 'Point' -- entirely within one binary's own DWARF, with
// no cross-module import involved at all.
#define TYPES_H
#include "types.h" // no-op: TYPES_H is already defined above

#ifndef TYPES_V2_H
#define TYPES_V2_H

struct Point {
  long x, y;
};

#endif // TYPES_V2_H

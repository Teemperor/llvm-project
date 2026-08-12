#include "types_v2.h"

// The other compile unit's view of the identically-named 'Point': 'x'/'y'
// are 'long' here, because types_v2.h pre-defines TYPES_H so that the
// #include "types.h" it performs is skipped, and instead manually
// redeclares 'Point' with different field types under its own guard
// macro (TYPES_V2_H). Both unit1.cpp and unit2.cpp compile fine on their
// own -- the ODR violation only becomes visible when comparing the DWARF
// these two compile units produce inside the *same* dylib.
extern "C" Point *f2();

Point gPoint2 = {10, 20};

Point *f2() { return &gPoint2; }

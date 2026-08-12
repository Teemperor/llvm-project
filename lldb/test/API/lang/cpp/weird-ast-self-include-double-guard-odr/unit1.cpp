#include "types.h"

// One compile unit's view of 'Point': 'x'/'y' are 'int'.
extern "C" Point *f1();

Point gPoint1 = {1, 2};

Point *f1() { return &gPoint1; }

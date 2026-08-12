#include "dylib2.h"

Cycle *gCycle2 = nullptr;

extern "C" {
void dylib2_init() { gCycle2 = new Cycle{2, 20}; }
}

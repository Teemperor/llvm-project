#include "dylib3.h"

Cycle *gCycle3 = nullptr;

extern "C" {
void dylib3_init() { gCycle3 = new Cycle{3, 30.0}; }
}

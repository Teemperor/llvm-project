#include "dylib1.h"

Cycle *gCycle1 = nullptr;

extern "C" {
void dylib1_init() { gCycle1 = new Cycle{1}; }
}

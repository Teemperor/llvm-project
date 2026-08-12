#include "base.h"

static Base gBase{42};

extern "C" {
void base_init(void) { gBase.id = 42; }
Base *base_get(void) { return &gBase; }
}

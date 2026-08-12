#include "b.h"

static B_Type gBTypeStorage;

extern "C" {

void b_init(void *aPtr) {
  gBTypeStorage.other = reinterpret_cast<Wrapper *>(aPtr);
  gBTypeStorage.b = 99;
}

B_Type *b_get_type(void) { return &gBTypeStorage; }
}

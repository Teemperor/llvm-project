#include "a.h"

static Wrapper gWrapperStorage;

extern "C" {

void a_init(void *bPtr) {
  gWrapperStorage.other = reinterpret_cast<B_Type *>(bPtr);
  gWrapperStorage.a = 42;
}

Wrapper *a_get_wrapper(void) { return &gWrapperStorage; }
}

#include "Left.h"

Box<int> *gLeftBox = nullptr;

extern "C" {
void left_init() {
  gLeftBox = new Box<int>();
  gLeftBox->val = 10;
  gLeftBox->tag = 100;
  gLeftBox->pad[0] = 'L';
}
}

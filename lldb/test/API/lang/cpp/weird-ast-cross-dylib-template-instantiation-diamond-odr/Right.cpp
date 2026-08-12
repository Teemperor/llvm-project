#include "Right.h"

Box<int> *gRightBox = nullptr;

extern "C" {
void right_init() {
  gRightBox = new Box<int>();
  gRightBox->val = 20;
  gRightBox->tag = 200;
  gRightBox->pad[0] = 'R';
}
}

#include <cstdint>

struct B1 {
  uint8_t a;
};

struct D1 : public B1 {
  uint8_t a;
  uint32_t ID;
  uint8_t b;
};

struct Mixin : public D1 {
  uint8_t a;
  uint32_t *arr[3];
};

struct B2 {
  uint32_t a;
};

class D2 : public B2, public Mixin {};

D2 d2g;

int main() {}

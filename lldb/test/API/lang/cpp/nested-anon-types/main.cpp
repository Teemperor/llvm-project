struct S {
  union {
    struct {
      unsigned u;
      float f;
    } UI;
    struct {
      int i;
      float f;
    } SI;
  };
};

int main() {
  S s;
  s.UI.u = 0x12345678;
  s.UI.f = 0.0f;
  return 0; // break here
}

// The three dylibs below each define an enum named 'Status' with a
// different scopedness and/or underlying type:
//   DylibX: enum class Status : uint8_t  { OK = 0, FAIL = 1 };  (scoped, 1 byte)
//   DylibY: enum class Status : int16_t  { OK = 0, FAIL = 1 };  (scoped, 2 bytes)
//   DylibZ: enum        Status           { OK = 0, FAIL = 1 };  (unscoped, implicit int)
//
// This mixes the "scoped vs. unscoped" ODR conflict (EnumDecl::isScoped())
// on top of a three-way underlying-type conflict. The main executable
// intentionally never sees any of the three definitions directly: it
// only forward-declares each dylib's *_init() function and calls through
// each dylib's global function pointer (gGetX/gGetY/gGetZ), which are
// only ever spelled with their real 'Status' return type inside the
// respective dylib's own debug info.
extern "C" {
void dylibX_init(void);
void dylibY_init(void);
void dylibZ_init(void);
}

// entry() is the actual breakpoint target used by the test: by the time
// it's hit, all three *_init() functions have run and gGetX/gGetY/gGetZ
// (declared in DylibX.h/DylibY.h/DylibZ.h, but never included here) are
// live and callable from the expression evaluator via debug info alone.
void entry() {}

int main() {
  dylibX_init();
  dylibY_init();
  dylibZ_init();
  entry();
  return 0;
}

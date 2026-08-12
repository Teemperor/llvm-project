// The main executable intentionally never sees either dylib's real
// definitions of 'RealA'/'RealB'/'Alias'. It only forward-declares the two
// tag types and re-derives the two pointer-returning entry points via
// their extern "C" names, so it never has to pick between the two mutually
// exclusive 'typedef ... Alias' declarations from DylibA.h/DylibB.h (which
// would be a hard compile error if #included together in one TU).
extern "C" {
struct RealA;
struct RealB;

RealA *getA(void);
RealB *getB(void);
RealB *getAliasBFromA(void);
RealA *getAliasAFromB(void);

void a_init(void);
void b_init(void);

void main_entry(void);
}

extern "C" void main_entry(void) {
  // By the time we get here, both dylibs have run their *_init functions
  // and getA()/getB() are ready to be called. This is where the test sets
  // its breakpoint.
}

int main() {
  a_init();
  b_init();
  main_entry();
  return 0;
}

// DylibA and DylibB both define classes named 'GrandBase', 'Mid1', 'Mid2'
// and 'Derived' (see DylibA.h/DylibB.h), but with fundamentally different
// inheritance layouts:
//   - DylibA: Mid1/Mid2 inherit *virtually* from GrandBase, so Derived has
//     a single, shared GrandBase subobject (classic virtual diamond).
//   - DylibB: Mid1/Mid2 inherit *non-virtually* from GrandBase, so Derived
//     has two separate GrandBase subobjects (classic non-virtual diamond).
//
// The main executable intentionally never includes either header: it only
// forward declares 'Derived' so it can hold one pointer per dylib. This
// mirrors real-world ODR violations where two shared libraries each ship
// their own (differently laid out) definition of the "same" class.
extern "C" {
void dylibA_init(void);
void dylibB_init(void);
}

struct Derived;

extern Derived *gDerivedA;
extern Derived *gDerivedB;

void entry() {
  // By the time we get here, both dylibs have run their *_init functions
  // and gDerivedA/gDerivedB point at differently-laid-out 'Derived'
  // instances.
}

int main() {
  dylibA_init();
  dylibB_init();
  entry();
  return 0;
}

// The three dylibs below each define a struct named 'Cycle' with a
// different layout (see dylib1.h/dylib2.h/dylib3.h). The main executable
// intentionally never sees any of those definitions: it only forward
// declares 'Cycle' so that it can hold one pointer per dylib. This
// mirrors a "naming cycle" where looking up the tag name 'Cycle' from
// any of the three modules could find any of the three (mutually
// incompatible) definitions.
struct Cycle;

extern "C" {
void dylib1_init(void);
void dylib2_init(void);
void dylib3_init(void);
}

extern Cycle *gCycle1;
extern Cycle *gCycle2;
extern Cycle *gCycle3;

void cycle_entry() {
  // By the time we get here, all three dylibs have run their *_init
  // functions and gCycle1/gCycle2/gCycle3 (one per dylib, each pointing
  // at a differently-shaped 'struct Cycle') are all initialized.
}

int main() {
  dylib1_init();
  dylib2_init();
  dylib3_init();
  cycle_entry();
  return 0;
}

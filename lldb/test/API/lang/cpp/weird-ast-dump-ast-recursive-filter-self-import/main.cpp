// The main executable intentionally never sees either dylib's
// definition of 'app::Ring': it only forward-declares the two
// allocator entry points, each returning an (from the main
// executable's point of view, incomplete) pointer to a type it never
// defines itself. This mirrors real-world scenarios where a single
// qualified name ('app::Ring') resolves to two independently-compiled,
// mutually incompatible definitions pulled in from two different
// shared libraries -- both of which happen to be self-referential
// (each has a field that is a pointer back to the very record being
// defined).
extern "C" {
void dylibA_init(void);
void dylibB_init(void);
}

void ring_entry() {
  // Breakpoint location. By the time we get here, both dylibs have run
  // their *_init functions (currently no-ops, but kept symmetrical with
  // sibling multi-dylib tests in case future revisions need per-dylib
  // setup).
}

int main() {
  dylibA_init();
  dylibB_init();
  ring_entry();
  return 0;
}

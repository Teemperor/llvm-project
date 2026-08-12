// The main executable never sees either dylib's definition of
// 'lib::Widget'. It only forward-declares the two helper functions
// ('make_a'/'make_b', which each return a pointer to their own dylib's
// 'lib::Widget') and the two dylib-owned globals ('gWidgetA'/'gWidgetB'),
// using an opaque 'void *' so that the main TU's own debug info carries
// no competing definition of 'lib::Widget' or 'lib'.
//
// DylibA defines:
//   namespace lib { inline namespace v1 { struct Widget { int a; }; } }
// DylibB defines:
//   namespace lib { inline namespace v2 {
//       struct Widget { int a; double b; }; } }
//
// Both 'v1' and 'v2' are marked 'inline' and each dylib's debug info
// claims to be *the* sole inline-namespace child of 'lib', but the two
// version names (and the shape of 'Widget' inside them) differ. This is
// an ODR violation that is invalid C++ but achievable purely at the
// DWARF/import level, since the two dylibs were compiled completely
// independently of one another.
extern "C" {
void dylibA_init(void);
void dylibB_init(void);
}

extern void *gWidgetA;
extern void *gWidgetB;

void ns_entry() {
  // By now both dylibA_init/dylibB_init have run, so gWidgetA and
  // gWidgetB point at two objects that both claim to have static type
  // 'lib::Widget', despite it being two different, ODR-incompatible
  // 'lib::Widget's stitched together only via debug info.
}

int main() {
  dylibA_init();
  dylibB_init();
  ns_entry();
  return 0;
}

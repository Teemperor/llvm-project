// The three dylibs below (DylibA/DylibB/DylibC) each define a struct
// named 'Triangle' with the same first field ('int x') but a mutually
// incompatible second field 'val':
//   DylibA: struct Triangle { int x; int val; };
//   DylibB: struct Triangle { int x; float val; };
//   DylibC: struct Triangle { int x; char val[8]; };
//
// The main executable never sees any of those definitions itself: it
// only forward declares 'Triangle' so that it can hold one pointer per
// dylib, obtained from each dylib's factory function.
struct Triangle;

extern "C" {
Triangle *make_triangle_a(void);
Triangle *make_triangle_b(void);
Triangle *make_triangle_c(void);
}

Triangle *gA = nullptr;
Triangle *gB = nullptr;
Triangle *gC = nullptr;

void triangle_entry() {
  // By this point gA/gB/gC point at three mutually-incompatible
  // 'struct Triangle' layouts, one per dylib. Evaluating an expression
  // that dereferences all three in sequence forces LLDB's ASTImporter
  // to import/merge 'Triangle' three times into the shared per-target
  // scratch ASTContext, each time with a different, incompatible field
  // layout for the same RecordDecl name.
}

int main() {
  gA = make_triangle_a();
  gB = make_triangle_b();
  gC = make_triangle_c();
  triangle_entry();
  return 0;
}

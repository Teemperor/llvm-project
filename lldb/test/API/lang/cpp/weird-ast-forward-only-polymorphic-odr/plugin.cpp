#include "plugin.h"

// The only place in the whole program where 'Opaque' is a complete type.
// It has virtual methods, so it gets a vtable pointer and RTTI, making the
// record layout more interesting for the AST importer to reconstruct than a
// plain aggregate.
class Opaque {
public:
  Opaque(int v) : value(v) {}
  virtual ~Opaque() {}
  virtual int getValue() const { return value; }

  int value;
};

// A real instance of the complete type, only ever known as 'Opaque *' on the
// main-executable side.
static Opaque *gRealOpaque = nullptr;

extern "C" {

void *make_opaque(void) {
  gRealOpaque = new Opaque(42);
  return gRealOpaque;
}

void plugin_init(void) { make_opaque(); }

void plugin_entry(void) {
  // Breakpoint location. By now gRealOpaque (and the exe-side global that
  // points at the same object) are fully set up.
}

} // extern "C"

// Regression test for a class whose first dynamic base is "nearly-empty"
// (only a vtable pointer, no data). Per the Itanium C++ ABI, such a base is
// selected as the derived class's "primary base", and the derived class's own
// vtable-pointer slot at offset 0 is the *same storage* as the primary base's
// -- no second pointer is allocated. Clang's debug-info emitter knows this and
// does not emit an explicit `_vptr$Derived` DWARF member for the derived class
// at all.
//
// Compilers also never emit a DIE for an *unnamed* bitfield, so the anonymous
// 7-bit bitfield placed right after the (implicit) vtable pointer is invisible
// in DWARF too. Combined, LLDB's unnamed-bitfield-gap reconstruction used to
// have nothing in its model -- no field, no explicit vtable-pointer member --
// to explain the first pointer-width bits of the object, producing a
// self-inconsistent layout that crashed Clang's expression-evaluator codegen.
// See other-bugs/typesystemclang-virtual-primary-base-bitfield-gap/README.md.
//
// Covers both a virtual and a non-virtual base -- Itanium's primary-base
// selection picks a nearly-empty base as primary either way.
struct Interface {
  virtual ~Interface() {}
  virtual int foo() const { return 1; }
};

struct VirtualBase : public virtual Interface {
  short : 7;
  short x : 3;
  char y;
  VirtualBase() : x(3), y(2) {}
};

struct NonVirtualBase : public Interface {
  short : 7;
  short x : 3;
  char y;
  int z;
  NonVirtualBase() : x(3), y(3), z(4) {}
};

VirtualBase g_virtual;
NonVirtualBase g_nonvirtual;

void stop() {}

int main() {
  stop();
  return g_virtual.foo() + g_nonvirtual.foo(); // break here
}

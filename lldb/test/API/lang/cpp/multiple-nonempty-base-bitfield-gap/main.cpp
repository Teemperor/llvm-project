// Regression test for a class with two ordinary (non-virtual) polymorphic
// base classes, *neither* of which is "nearly-empty" (each has its own
// explicit vtable pointer AND real data of its own beyond it, so there is no
// implicit-vtable-pointer-sharing question at all -- see
// other-bugs/typesystemclang-virtual-primary-base-bitfield-gap/README.md for
// that narrower, already-fixed sibling case). `Derived`'s own first declared
// field is a *named* bitfield (`ring_`) preceded by *unnamed* bitfield gaps
// in the source -- compilers never emit a DWARF DIE for an unnamed bitfield,
// so LLDB has to reconstruct the gap from the space between "the end of the
// previous member" (here, the end of the two base classes) and `ring_`'s
// real, DWARF-given absolute offset.
//
// `ring_`'s absolute offset (bit 318) is not byte-aligned. The reconstructed
// unnamed-bitfield-gap logic used to unconditionally suppress synthesizing
// any padding for a record's first own field whenever the record has any
// base at all and no *narrower* anchor (an explicit local vtable-pointer
// member, or an implicitly-shared one from a nearly-empty primary base)
// happened to cover the gap -- neither applies here, since both `Base1` and
// `Base2` have their own local vtable pointers and are not nearly-empty. That
// left `ring_` placed at its raw, non-byte-aligned offset with nothing
// synthesized before it, handing Clang's expression-evaluator codegen a
// self-inconsistent layout that crashed:
//
//   Assertion failed: ((BitOffset % CharBits) == 0 && "Not at start of
//   char"), function accumulateBitFields, file CGRecordLayoutBuilder.cpp,
//   line 546.
//
// Found by the torture fuzzer (torture/findings/crash-seed377059235-prog016,
// a `kmodel::Process : public KObject, public Schedulable` class); this is a
// hand-reduced minimal repro of the same DWARF shape (confirmed via
// llvm-dwarfdump: both reproduce a first bitfield at absolute bit offset
// 0x13e == 318, immediately after two 16-byte non-virtual bases).
struct Base1 {
  virtual ~Base1() {}
  virtual int id() const { return 1; }
  unsigned serial_ = 10;
};

struct Base2 {
  virtual ~Base2() {}
  virtual int prio() const { return 2; }
  short cls_ = 20;
  int nice_ = 30;
};

struct Derived : public Base1, public Base2 {
  int : 30;      // anonymous bitfield -- DWARF never records this
  unsigned : 30; // anonymous bitfield -- DWARF never records this
  unsigned ring_ : 2;
  unsigned kernel_ : 1;
  unsigned flags_ : 5;
  int tail = 99;
  Derived() {
    ring_ = 3;
    kernel_ = 0;
    flags_ = 5;
  }
};

Derived g_derived;

int main() {
  return g_derived.id() + g_derived.prio(); // break here
}

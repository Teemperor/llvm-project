// Regression test for a *bitfield* that starts inside the storage of the
// member declared before it, which the Itanium ABI produces whenever that
// member is `[[no_unique_address]]` and has tail padding to spare: `in` below
// has sizeof 8 but dsize 5, so `b` is placed at byte 5 -- inside `in`'s
// sizeof, past its dsize. (libc++'s `__compressed_pair` relies on exactly this
// reuse, which is why it shows up in real programs and not just in fuzzed
// ones.)
//
// DWARF cannot spell `[[no_unique_address]]`, so LLDB has to infer it from the
// offsets: a member starting inside the previous one means the previous one was
// potentially-overlapping, and Clang has to be told that or its record lowering
// gives that member its full sizeof worth of storage and then finds the next
// member sitting inside it:
//
//   Assertion failed: (M.Offset >= Tail && "Bitfield access unit is not
//   clipped"), function checkBitfieldClipping, file
//   CGRecordLayoutBuilder.cpp, line 960.
//
// That inference used to run for plain fields only, on the grounds that "a
// bitfield's access unit is Clang's own business". It is not entirely: Clang
// picks how *wide* the access unit wrapping a bitfield run is, but the unit
// still starts at the byte holding the run's first bit, so a bitfield has to
// clear the preceding member's storage just like a plain field does.
//
// Found by the fuzzer in cpp-test/findings/crash-seed427096881-prog019 (a
// `cryptolab::TestVector` whose deliberately ODR-inconsistent debug info put a
// named bitfield inside a base class subobject instead -- see
// ClangASTGeneratorOverlapTest.BitfieldInsideBaseIsDropped for that shape,
// which no valid source can produce). This is the same bug reached from valid
// C++.
struct Inner {
  int i;
  char c;
  // A user-declared destructor makes `Inner` non-POD, which is what allows its
  // tail padding to be reused at all.
  ~Inner() {}
};

struct Outer {
  [[no_unique_address]] Inner in; // at 0: sizeof 8, dsize 5
  unsigned b : 3;                 // at bit 40 (byte 5), inside `in`'s sizeof
  unsigned c : 5;                 // at bit 43, sharing b's access unit
  int t;
};

Outer g_outer{{1, 'x'}, 3, 7, 42};

int main() {
  return g_outer.b + g_outer.c + g_outer.t; // break here
}

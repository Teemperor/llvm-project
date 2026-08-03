// Regression test for a gap between a non-bitfield field and a following
// bitfield run that is narrower than a full "word" (32 bits).
//
// Compilers omit DWARF DIEs for *unnamed* bitfields, so LLDB reconstructs the
// gap they leave from the space between "the end of the previous member" and
// the next (named) bitfield's real DWARF-given offset. The reconstruction
// used to unconditionally round a preceding *non-bitfield* member's end up to
// the next 32-bit word boundary before doing that comparison, on the (wrong,
// in general) assumption that the non-bitfield's own trailing alignment
// padding always reaches a full word. Real (Itanium ABI) bit-field packing
// does not: `a` below ends at bit 8, but the invisible `unsigned : 6`
// anonymous bitfield leaves `b` sitting at a real offset of bit 14 -- before
// the rounded-up word boundary (32) the old heuristic used. That made the
// reconstruction believe there was no gap to fill at all, handing Clang's
// expression-evaluator codegen a bitfield "starting" a fresh run at a
// non-byte-aligned offset with no padding before it, which asserts in
// CGRecordLayoutBuilder.cpp (accumulateBitFields, "Not at start of char").
// See other-bugs/typesystemclang-bitfield-gap-word-rounding/README.md.
struct NarrowGap {
  char a;
  unsigned : 6; // anonymous bitfield -- DWARF never records this
  unsigned b : 3;
  int trigger; // forces the record to actually be laid out on completion
  NarrowGap() : a(1), b(2), trigger(3) {}
};

// A second shape covering the case this heuristic really was modeling
// correctly (a non-bitfield's tail padding genuinely does reach a full word,
// because the following bitfield is itself full-width) -- make sure removing
// the word-rounding step didn't just trade one bug for the opposite one. Here
// the real DWARF-given offset of `y` (32) already exceeds the raw,
// non-rounded gap start (8), so the gap is detected either way; this is a
// non-regression check, not a repro of the original bug.
struct FullWordGap {
  char x;
  unsigned y : 32;
  int trigger;
  FullWordGap() : x(4), y(5), trigger(6) {}
};

NarrowGap g_narrow;
FullWordGap g_fullword;

void stop() {}

int main() {
  stop();
  return g_narrow.a + g_narrow.b + g_narrow.trigger + g_fullword.x +
         g_fullword.y + g_fullword.trigger; // break here
}

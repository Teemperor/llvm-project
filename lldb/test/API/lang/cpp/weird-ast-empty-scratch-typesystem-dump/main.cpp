// Deliberately tiny/uninteresting C++ program. The point of this test is
// *not* to exercise any ODR conflict or ASTImporter merge -- it is to poke
// at 'target dump typesystem' (and 'target modules dump ast') at points in
// the debug session where LLDB's per-target shared scratch
// TypeSystemClang/ASTContext has not been lazily created yet (before any
// expression, breakpoint hit, or 'run'), or where the process backing the
// current target has already been torn down (after normal exit, or after
// 'kill'). Neither of those states should ever crash the dump commands --
// at worst they should print a clean "nothing there yet" style message.
class Simple {
public:
  int value;
  Simple(int v) : value(v) {}
  int getValue() const { return value; }
};

int main() {
  Simple s(42);
  return s.getValue() - 42;
}

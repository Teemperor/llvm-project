// Has intentionally the same name as the function-local class. LLDB should
// never pull in this definition as this test only touches the classes
// defined in the function.
struct ForwardConflict {
  int false_def;
};
ForwardConflict conflict;

int main() {
  struct WithMember {
    int i;
  };
  typedef struct { int i; } Anon;
  struct Forward;
  struct ForwardConflict;

  WithMember m;
  m.i = 1;
  Anon anon;
  anon.i = 2;
  Forward *fwd = nullptr;
  ForwardConflict *fwd_conflict = nullptr;
  return m.i; // break here
}

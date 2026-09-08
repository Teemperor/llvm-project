namespace outer {
inline namespace inner {
struct S {
  int v;
};

int fn(int x) { return x + 1; }
} // namespace inner

// An overload of the same name in the *enclosing* namespace, so that looking
// `fn` up in `outer` (which clang does on its own once a decl for `fn` is filed
// in the inline namespace `inner`) finds something.
int fn(double x) { return static_cast<int>(x) + 100; }
} // namespace outer

// Reaching `outer::inner::S` through a member makes the expression evaluator
// materialize the inline namespace while *generating a type*, before any name is
// looked up in it.
struct Wrapper {
  outer::inner::S s;
};

int main() {
  Wrapper w{{7}};
  int a = outer::inner::fn(1);
  int b = outer::fn(2.0);
  return a + b + w.s.v; // break here
}

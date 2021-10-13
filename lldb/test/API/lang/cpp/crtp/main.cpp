template <typename T> struct Base {
  T &ref;
  T *pointer;
  Base(T &t) : ref(t), pointer(&t) {}
  T func() { return ref; }
};

struct X : Base<X> {
  X() : Base<X>(*this) {}
  int member = 0;
};

X x;

int main() { return x.member; }

namespace ns {
struct Outer {
  struct Inner {
    int x = 1;
  };
  enum NestedEnum { NestedValue = 3 };
};
} // namespace ns

// Global types with the same unqualified names as the nested ones above.
struct Inner {
  double a = 2.5;
  double b = 3.5;
};
enum NestedEnum { GlobalValue = 7 };

Inner global_inner;
NestedEnum global_enum = GlobalValue;

int main() {
  ns::Outer::Inner nested;
  ns::Outer::NestedEnum nested_enum = ns::Outer::NestedValue;
  nested.x = 42;
  return nested.x + global_inner.a + nested_enum + global_enum; // break here
}

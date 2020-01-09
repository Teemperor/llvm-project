void foo(int x) {}

struct FooBar {
  int i;
};

enum class EnumInSource {
  A, B, C
};

template<typename T>
void TemplateFunc() {}

template<typename T>
struct TemplateStruct {};

int main() {
  FooBar f;
  foo(1);
  EnumInSource e = EnumInSource::A;
  TemplateFunc<int>();
  TemplateStruct<int> temp_struct;
  return 0; // Break here
}

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

struct SingleMember {
  int i;
};

struct BaseClass {
  long x;
};

struct Outer : BaseClass {
  struct SingleMember m;
};

// A class template; the debug info describes the instantiation Wrapper<int>
// as a class type named "Wrapper<int>".
template <typename T> struct Wrapper {
  T value;
  int tag;
};

// A class template with a non-type (value) template parameter. The debug info
// describes FixedArray<3> as a class type whose name embeds the value "3" and
// which has a DW_TAG_template_value_parameter child.
template <int N> struct FixedArray {
  int data[N];
  int size;
};

// A typedef alias.
typedef int MyInt;

// An unscoped enum with a gap in its values, and a scoped enum (enum class).
enum Color { Red, Green = 5, Blue };
enum class Fruit { Apple, Banana = 20 };

// A union: all members share the same storage.
union Number {
  int i;
  float f;
};

int main() {
  int local = 20;

  struct Outer outer;
  outer.m.i = 4;
  outer.x = -22;
  Outer *ptr = &outer;

  // Reference types: an lvalue reference to a scalar, an lvalue reference to a
  // record, and an rvalue reference.
  int scalar = 99;
  int &ref = scalar;
  Outer &outer_ref = outer;
  int &&rref = 123;

  // A template class instantiation.
  Wrapper<int> wrapper;
  wrapper.value = 7;
  wrapper.tag = -1;

  // A template class instantiation with a non-type template parameter.
  FixedArray<3> fixed;
  fixed.data[0] = 10;
  fixed.data[1] = 20;
  fixed.data[2] = 30;
  fixed.size = 3;

  // A typedef.
  MyInt my_int = 55;

  // A const-qualified scalar.
  const int const_int = 42;

  // Enumerations (unscoped and scoped).
  Color color = Green;
  Fruit fruit = Fruit::Banana;

  // A union.
  Number number;
  number.i = 65;

  // Standard-library containers. These generate large, deeply-nested template
  // types (typedefs, pointers, base classes, unions, ...) in the debug info.
  std::string str = "hello";
  std::vector<int> vec = {10, 20, 30};
  std::map<int, int> tree_map;
  tree_map[1] = 100;
  tree_map[2] = 200;
  std::unordered_map<int, int> hash_map;
  hash_map[7] = 700;

  return const_int + my_int + ref + rref + outer_ref.m.i + wrapper.value +
         fixed.data[1] + fixed.size + (color == Green) +
         (fruit == Fruit::Banana) + number.i + (int)str.size() + vec[0] +
         tree_map[1] + hash_map[7]; // break here
}

#include <unordered_set>

struct Obj {
  int value = 0;
  Obj(int v) : value(v) {}
  bool operator==(const Obj &o) const { return value == o.value; }
};

struct ObjHash {
  std::size_t operator()(const Obj &o) const noexcept {
    return o.value;
  }
};

int main(int argc, char **argv) {
  std::unordered_set<int> int_set = {1, 3};
  std::unordered_set<Obj, ObjHash> obj_set = {Obj(1), Obj(2)};
  // FIXME: std::hash specialization isn't supported yet.

  return 0; // Set break point at this line.
}

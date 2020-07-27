#include <unordered_map>

struct Key {
  int value = 0;
  Key(int v) : value(v) {}
  bool operator==(const Key &k) const { return value == k.value; }
};

struct KeyHash {
  std::size_t operator()(const Key &k) const noexcept {
    return k.value;
  }
};

int main(int argc, char **argv) {
  std::unordered_map<int, int> int_map = {{1, 2}, {2, 4}};
  std::unordered_map<Key, int, KeyHash> key_map = {{Key(1), 2}, {Key(2), 4}};
  // FIXME: std::hash specialization isn't supported yet.

  return 0; // Set break point at this line.
}

#include <map>

struct C {
  int i;
};

struct MyKey {
  int i;
};

struct MyKeyCompare {
  bool operator() (const MyKey& lhs, const MyKey& rhs) const {
    return lhs.i < rhs.i;
  }
};

int main(int argc, char **argv) {
  MyKey key1 = {1};
  MyKey key2 = {2};
  MyKey key3 = {3};
  std::map<int, C> map_with_dbg_value;
  map_with_dbg_value[1] = {1};


  std::map<MyKey, int, MyKeyCompare> map_with_dbg_key;
  map_with_dbg_key[key2] = 2;
  return 0; // Set break point at this line.
}

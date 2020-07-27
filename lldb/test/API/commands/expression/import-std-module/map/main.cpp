#include <map>

struct KeyWithOpMember {
  int value = 3;
  KeyWithOpMember(int v) : value(v) {}
  bool operator<(const KeyWithOpMember &k) const {
    return value < k.value;
  }
};

struct KeyWithGlobalOp {
  int value = 3;
  KeyWithGlobalOp(int v) : value(v) {}
  bool operator<(const KeyWithGlobalOp &k) const {
    return value < k.value;
  }
};

int main(int argc, char **argv) {
  std::map<int, int> int_map = {{1, 2}, {2, 4}};
  std::map<KeyWithOpMember, int> op_member_map = {{KeyWithOpMember(1), 2}, {KeyWithOpMember(2), 4}};
  std::map<KeyWithGlobalOp, int> op_global_map = {{KeyWithGlobalOp(1), 2}, {KeyWithGlobalOp(2), 4}};

  return 0; // Set break point at this line.
}

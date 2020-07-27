#include <set>

struct ValueWithOpMember {
  int value = 3;
  ValueWithOpMember(int v) : value(v) {}
  bool operator<(const ValueWithOpMember &k) const {
    return value < k.value;
  }
};

struct ValueWithGlobalOp {
  int value = 3;
  ValueWithGlobalOp(int v) : value(v) {}
};

bool operator<(const ValueWithGlobalOp &l, const ValueWithGlobalOp &r) {
  return l.value < r.value;
}

int main(int argc, char **argv) {
  std::set<int> int_set = {1, 2, 4};
  std::set<ValueWithOpMember> op_member_set = {ValueWithOpMember(1), ValueWithOpMember(2)};
  std::set<ValueWithGlobalOp> op_global_set = {ValueWithGlobalOp(1), ValueWithGlobalOp(2)};

  return 0; // Set break point at this line.
}

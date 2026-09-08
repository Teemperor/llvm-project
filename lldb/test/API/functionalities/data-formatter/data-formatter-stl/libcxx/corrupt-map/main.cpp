// Two well-formed maps that the test corrupts from the debugger: one gets a
// bogus node count only, the other additionally gets a cyclic __left_ chain.
#include <map>

int main() {
  std::map<int, int> inflated_size;
  inflated_size[1] = 11;
  inflated_size[2] = 22;
  inflated_size[3] = 33;

  std::map<int, int> cyclic_tree;
  cyclic_tree[1] = 11;
  cyclic_tree[2] = 22;
  cyclic_tree[3] = 33;

  return 0; // Set break point at this line.
}

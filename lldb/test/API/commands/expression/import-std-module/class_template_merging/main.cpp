#include <vector>

int main(int argc, char **argv) {
  // Makes sure we have the mock libc headers in the debug information.
  libc_struct s;
  std::unknown_container<int> v;
  return 0; // Set break point at this line.
}

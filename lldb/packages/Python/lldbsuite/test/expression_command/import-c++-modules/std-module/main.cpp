#include "foo.h"

// Necessary to include the 'std' module in the debug information
// of this translation unit.
// FIXME: This shouldn't be necessary.
//std::vector<int> v;

int main(int argc, char **argv) {
  // Set break point at this line.
  return std::abs(1);
}

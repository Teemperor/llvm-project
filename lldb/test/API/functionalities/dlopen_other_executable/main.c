#include <dlfcn.h>

int main() {
  int i = 0; // break here
  // dlopen the 'other' test executable.
  dlopen("other", RTLD_LAZY);
  return i;
}

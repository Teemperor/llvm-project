#include "plugin.h"

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <string>

// The two 'libfoo.dylib' images loaded into this process both define a
// class named 'Widget', but with two different, mutually incompatible
// layouts:
//   - the top-level libfoo.dylib (linked normally): class Widget { public:
//     int x; };
//   - the "hidden" libfoo.dylib (loaded explicitly via dlopen() with a full
//     path to a *different* on-disk directory, at runtime): class Widget {
//     public: long x; long y; };
//
// Crucially, both dylibs are built with the exact same LC_ID_DYLIB install
// name (@executable_path/libfoo.dylib) -- i.e. from dyld's point of view
// they claim to be "the same" shared library, even though they are two
// distinct files at two distinct paths with two conflicting definitions of
// 'Widget'. Loading the hidden one via dlopen() with an explicit full path
// bypasses the normal load-time dedup-by-install-name that would otherwise
// collapse them into a single loaded image, so LLDB ends up with two
// distinct Module objects that both report the exact same basename
// ("libfoo.dylib"), and, on this platform, the exact same install name.
//
// This makes 'target modules dump ast libfoo.dylib' (and any other
// by-name module lookup) genuinely ambiguous: the lookup-by-basename code
// path in LLDB has to deal with more than one matching Module for the
// same name at the same time.

// Global pointers into each dylib's 'Widget', typed as 'void *' here
// since the main executable never includes either dylib's conflicting
// 'Widget' definition itself. The test evaluates expressions that cast
// these back to 'Widget *' from each dylib's own debug info.
void *gTopWidget = nullptr;
void *gHiddenWidget = nullptr;

// Breakpoint location: hit only after both conflicting 'libfoo.dylib'
// images (and their respective 'Widget' globals) have been loaded and
// initialized.
void main_entry() {}

int main(int argc, char **argv) {
  // Load the top-level libfoo.dylib's side of things. This dylib is
  // already loaded (it is linked directly into this executable), so this
  // just runs its initializer and hands back a Widget* into it.
  void *top_widget = libfoo_top_init();
  if (top_widget == nullptr) {
    fprintf(stderr, "libfoo_top_init failed\n");
    return 1;
  }
  gTopWidget = top_widget;

  // Now load the "hidden" copy of libfoo.dylib -- built from different
  // source, with a conflicting 'Widget' definition, but installed under
  // the exact same LC_ID_DYLIB name -- via dlopen() using an explicit
  // full path that points at a *different* directory than the top-level
  // one. This is the load-time trick that gets two same-named,
  // ODR-conflicting dylibs loaded into the same process at once.
  //
  // argv[0] is the path to the built 'a.out'; the "hidden" dylib is a
  // sibling "hidden/libfoo.dylib" placed next to it by the Makefile.
  std::string exe_path = argv[0];
  std::string dir = exe_path.substr(0, exe_path.find_last_of('/'));
  std::string hidden_dylib_path = dir + "/hidden/libfoo.dylib";

  void *handle = dlopen(hidden_dylib_path.c_str(), RTLD_NOW);
  if (handle == nullptr) {
    fprintf(stderr, "dlopen(%s) failed: %s\n", hidden_dylib_path.c_str(),
            dlerror());
    return 1;
  }

  typedef void *(*init_fn)();
  init_fn hidden_init =
      reinterpret_cast<init_fn>(dlsym(handle, HIDDEN_INIT_SYMBOL));
  if (hidden_init == nullptr) {
    fprintf(stderr, "dlsym(%s) failed: %s\n", HIDDEN_INIT_SYMBOL, dlerror());
    return 1;
  }

  void *hidden_widget = hidden_init();
  if (hidden_widget == nullptr) {
    fprintf(stderr, "libfoo_hidden_init failed\n");
    return 1;
  }
  gHiddenWidget = hidden_widget;

  // By the time we get here, both conflicting 'Widget' definitions are
  // loaded into the process and reachable via debug info: 'gTopWidget'
  // points at the top-level libfoo.dylib's 'class Widget { int x; }', and
  // 'gHiddenWidget' points at the hidden libfoo.dylib's
  // 'class Widget { long x; long y; }'.
  main_entry();

  printf("done\n");
  return 0;
}

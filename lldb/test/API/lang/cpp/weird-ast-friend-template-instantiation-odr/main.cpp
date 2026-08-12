#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template 'Holder' instantiated with the exact
// same template argument (int), and both grant friendship to a class
// 'Peeker' whose member function 'get' has a different signature/return
// type in each translation unit. This forces Sema's access-control and
// redeclaration-chain logic to resolve 'Peeker' as a friend of an
// *instantiated* 'Holder<int>' independently in each TU, and forces the
// ASTImporter to import/merge both the FriendDecl and the separately
// instantiated 'Holder<int>' ClassTemplateSpecializationDecl into the
// shared scratch AST context.
template <typename T> class Holder {
  T v;
  friend class Peeker;
};

class Peeker {
public:
  static int get(Holder<int> &h);
};

int Peeker::get(Holder<int> &h) { return 1; }

Holder<int> ha;

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

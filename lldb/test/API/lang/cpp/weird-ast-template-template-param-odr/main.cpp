#include "plugin.h"

// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define a class template 'Box', and a second class template
// 'Holder' that takes 'Box' itself (a class template, not a type) as a
// *template template argument*. Both modules instantiate the exact same
// specialization 'Holder<Box, int>', but the two modules' 'Box' bodies
// disagree (plugin.cpp's 'Box' has an extra leading field), which makes
// this a genuine ODR violation on 'Box<int>' - reached only indirectly,
// through expanding the template-template argument 'Box' inside
// 'Holder<Box, int>'.
//
// 'Holder<Box, int>' is instantiated with a TemplateArgument of kind
// 'Template' (the class template 'Box') rather than the far more common
// kind 'Type'. That is a much less exercised path of Clang's ASTImporter:
// importing/comparing a TemplateArgument::Template means importing the
// referenced ClassTemplateDecl and, transitively, whichever of its
// specializations get instantiated (here 'Box<int>'), rather than just
// importing a QualType. A structural mismatch discovered only while
// expanding that nested specialization is exactly the kind of corner case
// that risks tripping an unexpected assertion or unreachable deep inside
// ASTNodeImporter::VisitClassTemplateSpecializationDecl, or inside the
// TemplateArgument structural/profile comparison logic, instead of a clean
// ODR diagnostic.
template <typename T> struct Box {
  T val;
};

template <template <typename> class C, typename U> struct Holder {
  C<U> inner;
  int flag;
};

// Implicitly instantiates 'Holder<Box, int>' (and, transitively,
// 'Box<int>') using main.cpp's definition of 'Box'.
Holder<Box, int> main_holder = {{111}, 1};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

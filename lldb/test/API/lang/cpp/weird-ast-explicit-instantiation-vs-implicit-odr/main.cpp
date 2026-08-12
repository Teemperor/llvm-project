#include "plugin.h"

// Primary class template. Deliberately not shared via a common header with
// plugin.cpp: each module builds its own, independent notion of what
// 'Stat<double>' looks like.
//
// This module additionally provides an *explicit instantiation definition*
// of 'Stat<double>' right after the template - see plugin.cpp for the
// crucial twist: the dylib only ever *implicitly* instantiates the same
// template-id from its own (differently laid out) primary template.
//
// Clang represents these very differently in the AST even though they
// describe "the same" type:
//   - main executable: a ClassTemplateSpecializationDecl for 'Stat<double>'
//     with TemplateSpecializationKind TSK_ExplicitInstantiationDefinition,
//     with members 'sum' then 'count' (in that order).
//   - dylib: a ClassTemplateSpecializationDecl for 'Stat<double>' with
//     TemplateSpecializationKind TSK_ImplicitInstantiation, with members
//     'count' then 'sum' (the fields are deliberately reordered relative to
//     the main executable's template).
//
// This is a real ODR violation: the same template-id has two incompatible
// definitions (different member order, hence different layout) across
// translation units, compounded by disagreeing about how the specialization
// came to exist in the first place (explicit-instantiation-definition
// bookkeeping vs. plain implicit instantiation). The hope is that forcing
// DWARFASTParserClang / TypeSystemClang to decide whether the "already
// complete" specialization it parsed from one module can be reused, reused
// with additional completion, or must be re-instantiated when the *other*
// module's conflicting definition of the identical template-id shows up,
// trips over that mismatched specialization-kind bookkeeping badly enough
// to crash instead of just returning a wrong-but-well-formed layout.
template <typename T> struct Stat {
  T sum;
  T count;
};
template struct Stat<double>;

// Explicit instantiation definition of 'Stat<double>' above, used here.
Stat<double> main_stat = {1.0, 2.0};

int main() {
  plugin_init();
  plugin_entry();
  return 0;
}

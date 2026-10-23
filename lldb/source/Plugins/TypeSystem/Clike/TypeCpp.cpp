//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeCpp.h"

#include "Context.h"
#include "TypeName.h"

using namespace lldb_private::clike;

// Storage for the LLVM RTTI discriminators. The addresses (not the values) are
// what identify each class, so the initializer is irrelevant.
char ClassType::ID = 0;
char ReferenceType::ID = 0;
char MemberPointerType::ID = 0;

std::optional<uint64_t> ClassType::GetAlignmentInBits() const {
  uint64_t min_alignment = 8;
  if (IsPolymorphic())
    min_alignment = GetOwningContext()
                        .GetLanguageOpts()
                        .GetBuiltinAlignments()
                        .pointer_align;
  return ComputeAlignmentInBits(min_alignment);
}

// Render a single template argument for the display name. `opts` is threaded
// into any nested type arguments so a fully-qualified name keeps defaulted
// template arguments while a simplified display name drops them, and so
// inline namespaces are kept when reconstructing the fully-qualified
// (non-display) name, which must keep them (e.g. `std::__1::`) since data
// formatters key on that raw spelling.
static std::string BuildTemplateArgName(const TemplateArgument &arg,
                                        const DisplayNameOptions &opts) {
  // A type argument always has a type (a `void` argument names the `void`
  // builtin); only a template-template argument, handled next, has none.
  if (arg.kind == lldb::eTemplateArgumentKindType)
    return arg.type->Get().GetDisplayName(opts);
  // A template-template argument (e.g. `T1` in `C<float, T1>`) is kept by name
  // only; print that name.
  if (arg.kind == lldb::eTemplateArgumentKindTemplate)
    return arg.name.GetName().str();
  // Integral (non-type) argument: render according to the argument's type,
  // mirroring clang's TemplateArgument::print.
  if (!arg.type)
    return std::to_string(static_cast<int64_t>(arg.integral_value));
  return arg.type->Get().GetTemplateArgValueName(arg.integral_value, opts);
}

std::string
ClassType::BuildTemplateArgList(const DisplayNameOptions &opts) const {
  std::string args;
  for (const TemplateArgument &arg : m_template_args) {
    if (opts.hide_default_args && arg.is_default)
      continue;
    if (!args.empty())
      args += ", ";
    args += BuildTemplateArgName(arg, opts);
  }
  // No (non-default) arguments, e.g. a specialization over an empty pack. Clang
  // still prints an empty argument list: `TypePack<>`.
  if (args.empty())
    return "<>";
  // Match clang's default printing policy (SplitTemplateClosers): put a space
  // between two consecutive closing angle brackets so a nested instantiation
  // prints as `Foo<Foo<int> >`, not `Foo<Foo<int>>`.
  std::string closer = args.back() == '>' ? " >" : ">";
  return "<" + args + closer;
}

std::string ClassType::GetDisplayName(const DisplayNameOptions &opts) const {
  // For a class-template instantiation we have modeled args, so reconstruct
  // "base<non-default args>". This is also done for a specialization over an
  // empty parameter pack (`TypePack<>`), which has zero arguments but still
  // prints an (empty) `<>`. Otherwise use the unqualified spelling verbatim
  // (this also covers not-yet-completed templates, whose args aren't parsed).
  llvm::StringRef unqualified = GetUnqualifiedName().GetName();
  if (!IsTemplateInstantiation() || unqualified.empty())
    return RecordType::GetDisplayName(opts);
  return BuildScopePrefix(*this, opts.keep_inline_namespaces) +
         unqualified.substr(0, unqualified.find('<')).str() +
         BuildTemplateArgList(opts);
}

std::string ClassType::GetCanonicalName(bool base_only) const {
  // The name normally comes verbatim from the debug info (see
  // Type::GetCanonicalName). The one exception is a class-template
  // instantiation whose DWARF spelling diverges from clang's (see
  // NeedsTemplateNameReconstruction): only in that case do we reconstruct the
  // name from the modeled template arguments. The fully-qualified name keeps
  // defaulted arguments; only the display name drops them.
  if (!NeedsTemplateNameReconstruction())
    return RecordType::GetCanonicalName(base_only);
  llvm::StringRef unqualified = GetUnqualifiedName().GetName();
  DisplayNameOptions opts;
  opts.hide_default_args = false;
  opts.keep_inline_namespaces = true;
  std::string name = unqualified.substr(0, unqualified.find('<')).str() +
                     BuildTemplateArgList(opts);
  if (base_only)
    return name;
  // Fully-qualified: prefix the enclosing namespace/class scopes, keeping
  // inline namespaces (unlike the display name) since this is the raw
  // spelling data formatters key on.
  return BuildScopePrefix(*this, /*keep_inline_namespaces=*/true) + name;
}

// Two cases diverge from clang's own printing (TemplateArgument::print, which
// BuildTemplateArgName mirrors): an argument whose type spells it differently
// in DWARF (see Type::HasDivergentTemplateArgSpelling), and a type argument
// whose own name needs reconstructing (`Foo<Bar<1L>>`): `Bar<1L>`'s own
// spelling embeds the divergent argument, so `Foo`'s DWARF name is wrong even
// though none of `Foo`'s own template arguments is integral.
bool ClassType::NeedsTemplateNameReconstruction() const {
  if (!IsTemplateInstantiation())
    return false;
  for (const TemplateArgument &arg : m_template_args) {
    if (arg.kind == lldb::eTemplateArgumentKindType) {
      if (arg.type->Get().NeedsTemplateNameReconstruction())
        return true;
      continue;
    }
    if (arg.kind != lldb::eTemplateArgumentKindIntegral)
      continue;
    if (arg.type && arg.type->Get().HasDivergentTemplateArgSpelling())
      return true;
  }
  return false;
}

std::string
ReferenceType::GetDisplayName(const DisplayNameOptions &opts) const {
  return GetPointeeType()->GetPointeeDisplayName(m_is_rvalue ? "&&" : "&",
                                                 opts);
}

std::string ReferenceType::GetCanonicalName(bool base_only) const {
  return GetPointeeType()->GetPointeeCanonicalName(m_is_rvalue ? "&&" : "&",
                                                   base_only);
}

//===-- TypeName.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Rendering a clike_typesystem type back to its source spelling. Two names are
// produced, mirroring the CompilerType API:
//
//   BuildCanonicalName -- the raw DWARF spelling, keeping defaulted template
//     arguments and inline namespaces. Data formatters match against exactly
//     this, so it must reproduce what the compiler emitted.
//
//   BuildDisplayName -- the user-facing spelling: defaulted template arguments
//     and inline namespaces are dropped, so `std::__1::vector<int,
//     std::__1::allocator<int>>` prints as `std::vector<int>`.
//
// This is a pure function of the type model -- nothing here needs the
// TypeSystem, apart from completing a lazily-parsed record before reading its
// template arguments, which the caller does.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_NAME_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_NAME_H

#include <string>

#include "llvm/ADT/StringRef.h"

namespace lldb_private {
namespace clike_typesystem {

class FunctionType;
class Type;

/// The type's canonical name: the spelling as the debug info recorded it, with
/// defaulted template arguments and inline namespaces kept. \p base_only asks
/// for the unqualified spelling (no enclosing namespace/class scopes), matching
/// clang's GetTypeNameForDecl(qualified=false).
///
/// The caller must have completed any class-template instantiation reachable
/// from \p t first (see TypeSystemClike::CompleteTemplateInstantiationForName):
/// an enum-typed non-type template argument is re-rendered from the modeled
/// arguments, which only exist after completion.
std::string BuildCanonicalName(Type *t, bool base_only);

/// The type's display name: like BuildCanonicalName but dropping defaulted
/// template arguments and inline namespaces. Also the form used for the
/// C-declarator spellings (`int (*)(const char *)`), which have no "raw DWARF
/// name" to preserve.
std::string BuildDisplayName(Type *t, bool hide_default_args = true,
                             bool keep_inline_namespaces = false);

/// Render a function signature in C declarator form, placing \p decl (e.g. ""
/// for a plain function, "(*)" for a function pointer, "(&)" for a reference)
/// between the return type and the parameter list: `int (*)(const char *)`.
/// Exposed for `target modules dump ast`, which renders a member function's
/// declaration the way clang::RecordDecl::print does.
std::string BuildFunctionName(FunctionType *fn, llvm::StringRef decl,
                              bool keep_inline_namespaces = false);

} // namespace clike_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_NAME_H

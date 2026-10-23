//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Rendering a clike type back to its source spelling. Two names are
// produced, mirroring the CompilerType API:
//
//   BuildCanonicalName -- the raw DWARF spelling, keeping inline namespaces.
//     Data formatters match against exactly this, so it must reproduce what
//     the compiler emitted.
//
//   BuildDisplayName -- the user-facing spelling: inline namespaces are
//     dropped.
//
// This is a pure function of the type model -- nothing here needs the
// TypeSystem.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_NAME_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_NAME_H

#include <string>

#include "llvm/ADT/StringRef.h"

namespace lldb_private {
namespace clike {

class Type;

/// The type's canonical name: the spelling as the debug info recorded it, with
/// inline namespaces kept. \p base_only asks for the unqualified spelling (no
/// enclosing namespace scopes), matching clang's
/// GetTypeNameForDecl(qualified=false).
std::string BuildCanonicalName(Type *t, bool base_only);

/// The type's display name: like BuildCanonicalName but dropping inline
/// namespaces. Also the form used for the C-declarator spellings
/// (`int (*)(const char *)`), which have no "raw DWARF name" to preserve.
std::string BuildDisplayName(Type *t, bool keep_inline_namespaces = false);

/// Render a function signature in C declarator form, placing \p decl (e.g. ""
/// for a plain function, "(*)" for a function pointer, "(^)" for a block
/// pointer) between the return type and the parameter list:
/// `int (*)(const char *)`. \p fn must be a function type (see
/// Type::IsFunction).
std::string BuildFunctionName(Type *fn, llvm::StringRef decl,
                              bool keep_inline_namespaces = false);

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_NAME_H

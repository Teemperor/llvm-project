//===-- ObjCMethodSignature.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Turning the Objective-C runtime's `@encode`-style type-encoding strings into
// cpp_typesystem types. This is how a class whose ivars/methods aren't in the
// debug info (a stripped image, a runtime-only class) gets a usable signature:
// the runtime reports each method as a string like "i16@0:8", which is parsed
// into per-argument encodings here and realized into real types.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_OBJCMETHODSIGNATURE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_OBJCMETHODSIGNATURE_H

#include "lldb/Symbol/CompilerType.h"

#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace lldb_private {
namespace cpp_typesystem {

class Builder;
class ObjCInterfaceType;

/// Splits a runtime method type-encoding string (e.g. "i16@0:8") into its
/// per-argument type substrings, discarding the stack-offset digits that follow
/// each one. Mirrors AppleObjCDeclVendor.cpp's ObjCRuntimeMethodType, which
/// does the same for building a clang::ObjCMethodDecl; this is a faithful port
/// to avoid subtly diverging on the (undocumented, encoding-specific) parsing
/// rules -- e.g. that a digit inside a `{...}`/`[...]`/`(...)` group is part of
/// the type (an array/bitfield count), not the argument's stack offset, so
/// brace-depth tracking is required to tell them apart.
class ObjCRuntimeMethodSignature {
public:
  explicit ObjCRuntimeMethodSignature(const char *types);

  /// Whether the encoding parsed cleanly. A false value means corrupt or
  /// unsupported runtime metadata; the type list is then meaningless.
  explicit operator bool() const { return m_is_valid; }

  size_t GetNumTypes() const { return m_types.size(); }
  llvm::StringRef GetTypeAtIndex(size_t idx) const { return m_types[idx]; }

private:
  std::vector<std::string> m_types;
  bool m_is_valid = false;
};

/// Create a complete, empty record named \p name for one of the opaque
/// Objective-C runtime types (`objc_object`, `objc_class`, `objc_selector`).
///
/// Those are declared but never defined in debug info. TypeSystemClang maps
/// `id`/`Class`/`SEL` onto complete builtin types, so a value of one can be
/// dereferenced without hitting an "incomplete type" error; mirror that by
/// making the opaque pointee a complete, empty record rather than a forward
/// declaration. It is given a one-byte size (the minimum for a complete record,
/// matching an empty C++ class) so dereferencing a pointer to it succeeds --
/// ValueObject treats a zero byte size as "no deref child".
CompilerType CreateOpaqueObjCRecordType(Builder &builder, llvm::StringRef name);

/// Realize a single Objective-C type-encoding (e.g. "i", "f", "^v", "@") into a
/// type created through \p builder, advancing \p enc past what it consumed.
/// Returns an empty CompilerType for an unrecognized encoding (notably a
/// struct-by-value `{...}`, which is not handled).
CompilerType RealizeObjCEncoding(Builder &builder, llvm::StringRef &enc);

/// Add \p iface's runtime-reported \p selector (as reported by
/// ObjCLanguageRuntime::ClassDescriptor::Describe's method callbacks) to
/// \p iface as a cpp_typesystem::ObjCMethod, realizing its signature from
/// \p type_encoding (the method's full `@encode`-style type-encoding string,
/// e.g. "i16@0:8"). Does nothing if \p type_encoding can't be fully decoded --
/// mirroring AppleObjCDeclVendor::ObjCRuntimeMethodType::BuildMethod, which
/// likewise drops a method it can't fully type rather than approximating it.
void AddRuntimeObjCMethod(Builder &builder, ObjCInterfaceType &iface,
                          llvm::StringRef class_name, const char *selector,
                          const char *type_encoding, bool is_class_method);

} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_OBJCMETHODSIGNATURE_H

//===-- CppObjCDeclVendor.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_CPPOBJCDECLVENDOR_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_CPPOBJCDECLVENDOR_H

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "lldb/Symbol/DeclVendor.h"

namespace lldb_private {

/// The TypeSystemCpp-backed counterpart to AppleObjCDeclVendor. Used by
/// AppleObjCRuntimeV2::GetDeclVendor() instead of AppleObjCDeclVendor when
/// symbols.enable-typesystem-cpp is on, so that the ObjC runtime's
/// non-expression-parsing consumers (SBTarget::FindFirstType/FindTypes, the
/// ObjC Language::TypeScavenger, AppleObjCRuntimeV2::GetDynamicTypeAndAddress's
/// CompilerType-only fallback) get a TypeSystemCpp CompilerType instead of
/// instantiating a private TypeSystemClang AST (as AppleObjCDeclVendor does).
///
/// This does not synthesize clang::ObjCInterfaceDecl/ObjCMethodDecl the way
/// AppleObjCDeclVendor does, so it cannot serve ClangASTSource's legacy ObjC
/// expression-parsing lookups (FindDeclInObjCRuntime/FindObjCMethodDecls).
/// That is fine: when symbols.enable-typesystem-cpp is on, CppExpressionDeclMap
/// is installed as the expression's external AST source instead of
/// ClangExpressionDeclMap, so ClangASTSource never runs and never calls
/// FindDecls on this vendor.
///
/// FindDecls() deliberately always returns 0 -- see its definition. This is
/// load-bearing, not just "unimplemented": ObjCLanguageRuntime::LookupInRuntime()
/// (used by GetRuntimeType(), which ValueObject::GetCompilerType() consults for
/// every ObjC-flagged value to see if the runtime has a "more complete" class
/// definition) calls FindDecls(), not FindTypes(). That mechanism has no "is
/// the runtime's answer actually better" comparison (unlike
/// TypeSystemCpp::GetRuntimeCompletedObjCType, which only prefers the runtime
/// when it has strictly more fields): it unconditionally overrides the value's
/// type with whatever comes back. A TypeSystemClang CompilerType from
/// AppleObjCDeclVendor is a type-system mismatch against a TypeSystemCpp value
/// and so is safely ignored downstream; a same-TypeSystem CompilerType from
/// this vendor would NOT be, and would get used for child navigation --
/// corrupting fields the runtime can only reconstruct crudely (id/Class/SEL
/// ivars become opaque untyped pointers, see
/// TypeSystemCpp::RealizeObjCEncoding). Making FindDecls() succeed here was
/// tried and broke TestObjCStepping/TestOrderedSet's KVO-`isa` field display.
///
/// The same opaque-pointer crudeness has one remaining, narrower fallout via
/// FindTypes(): AppleObjCRuntimeV2::GetDynamicTypeAndAddress's last-resort
/// "try to go for a CompilerType at least" fallback (used when a class has no
/// debug info at all) accepts this vendor's reconstruction, which is good
/// enough for most ObjC dynamic-value resolution but not for a container
/// class's own crudely-reconstructed `id`-typed storage -- see
/// TestDataFormatterObjCNSError/NSContainer (nested dictionary ivars display
/// as raw pointers instead of formatted contents). Gating that fallback to
/// AppleObjCDeclVendor only (mirroring the FindDecls situation above) was
/// tried and made things worse overall (it also starves ObjC dynamic-value
/// resolution that otherwise works fine through this vendor, regressing
/// several more data-formatter tests), so it is intentionally NOT gated.
/// This is a known, narrow, accepted gap alongside this project's other
/// tracked ObjC/data-formatter baseline gaps (see CLAUDE.md), not something
/// to "fix" by touching this fallback again without new evidence.
class CppObjCDeclVendor : public DeclVendor {
public:
  CppObjCDeclVendor(ObjCLanguageRuntime &runtime);

  static bool classof(const DeclVendor *vendor) {
    return vendor->GetKind() == eCppObjCDeclVendor;
  }

  uint32_t FindDecls(ConstString name, bool append, uint32_t max_matches,
                     std::vector<CompilerDecl> &decls) override;

  std::vector<CompilerType> FindTypes(ConstString name,
                                      uint32_t max_matches) override;

private:
  ObjCLanguageRuntime &m_runtime;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_CPPOBJCDECLVENDOR_H

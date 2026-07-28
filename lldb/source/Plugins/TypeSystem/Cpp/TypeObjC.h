//===-- TypeObjC.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Objective-C-only type kinds: an `@interface` and the methods it carries.
// See Type.h for the language-neutral core and TypeC.h / TypeCpp.h for the
// kinds specific to those languages.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_OBJC_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_OBJC_H

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "Identifier.h"
#include "Type.h"

namespace lldb_private {
namespace cpp_typesystem {

/// An Objective-C method of an interface. Only what an expression needs to
/// build a clang::ObjCMethodDecl and message-send it: the full method name
/// (`-[Class sel:]` / `+[Class sel:]`, which encodes both the selector and
/// whether it is an instance or class method), the (self/_cmd-stripped)
/// signature, and the asm label so the JIT can resolve the callee. Whether the
/// method is variadic matters for the message-send lowering.
struct ObjCMethod {
  /// The full name as written in the debug info, e.g. `+[Foo doThing:with:]`.
  Identifier name;
  /// The method's type (a FunctionType); its parameters exclude the implicit
  /// `self`/`_cmd` parameters.
  TypeRef type;
  /// The asm label (an lldb FunctionCallLabel) the call resolves through.
  Identifier asm_label;
  /// True for a class method (`+[...]`), false for an instance method.
  bool is_class_method = false;
  bool is_variadic = false;
  /// True for an `objc_direct` method: the compiler resolved every call
  /// statically to this implementation rather than going through
  /// objc_msgSend. The synthesized clang::ObjCMethodDecl must carry an
  /// ObjCDirectAttr so the expression parser's message-send codegen emits a
  /// direct call instead of a dynamic dispatch (which would send an
  /// unrecognized selector, since a direct method is never registered with
  /// the ObjC runtime).
  bool is_direct = false;
  /// True when the method's declared return type is `instancetype` (a related
  /// result type): the synthesized clang::ObjCMethodDecl must return
  /// `instancetype` with related-result-type set so a class-method send
  /// (`[NSURL URLWithString:...]`) types as the receiver class pointer
  /// (`NSURL *`) rather than a bare `id` -- otherwise dereferencing the result
  /// yields an unsized `id`. The stored FunctionType's return is a placeholder
  /// (`id`); the generator substitutes `instancetype`.
  bool returns_instancetype = false;
};

/// An Objective-C class type (`@interface Foo`). Its instance variables
/// (ivars) are modeled as record fields and its (single) superclass as its one
/// base class, so it reuses the RecordType field/base-class machinery.
///
/// Unlike a C/C++ record, an ObjC ivar's byte offset is NOT reliably present in
/// DWARF: the compiler emits 0 for every ivar (see the identical
/// DW_AT_data_member_location values in the debug info). The authoritative
/// offset lives in the Objective-C runtime -- specifically in the
/// `OBJC_IVAR_$_Class.ivar` offset symbols -- and is resolved against a live
/// process at value-inspection time (see
/// TypeSystemCpp::GetChildCompilerTypeAtIndex). The field byte_offset stored
/// here is therefore only a fallback.
class ObjCInterfaceType
    : public llvm::RTTIExtends<ObjCInterfaceType, RecordType> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassObjCObject;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsObjC | lldb::eTypeIsStructUnion;
  }

  // The superclass is exposed as this interface's single base class.
  uint32_t GetNumBaseClasses() const override { return m_superclass ? 1 : 0; }
  const BaseClass *GetBaseClassAtIndex(uint32_t idx) const override {
    if (idx == 0 && m_superclass)
      return &*m_superclass;
    return nullptr;
  }

  /// Objective-C methods of this interface (used to build ObjCMethodDecls so
  /// the expression parser can type-check and lower a message send). Parsed
  /// lazily alongside member functions (see AreMemberFunctionsParsed).
  uint32_t GetNumObjCMethods() const { return m_objc_methods.size(); }
  const ObjCMethod *GetObjCMethodAtIndex(uint32_t idx) const {
    if (idx < m_objc_methods.size())
      return &m_objc_methods[idx];
    return nullptr;
  }

private:
  // Gated like RecordType's mutators (see there): only Context, reached through
  // the locked Builder, may set the superclass.
  friend class Context;
  void SetSuperClass(TypeRef type) {
    m_superclass = BaseClass{type, /*byte_offset=*/0, /*is_virtual=*/false,
                             /*vbase_offset_offset=*/std::nullopt};
  }
  void AddObjCMethod(ObjCMethod method) {
    m_objc_methods.push_back(std::move(method));
  }

  std::optional<BaseClass> m_superclass;
  std::vector<ObjCMethod> m_objc_methods;
};

} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_OBJC_H

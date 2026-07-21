//===-- ClangTypeConverter.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangTypeConverter.h"

#include "ClangASTGenerator.h"

#include "Plugins/TypeSystem/Cpp/Builder.h"
#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecordLayout.h"

using namespace lldb_private;
using namespace lldb;
namespace ct = cpp_typesystem;

ClangTypeConverter::ClangTypeConverter(ClangASTGenerator &generator,
                                       TypeSystemCpp &target)
    : m_generator(generator), m_target(target), m_ast(generator.m_ast) {}

CompilerType ClangTypeConverter::Convert(clang::QualType qt) {
  if (qt.isNull())
    return {};

  // A deduced `auto`/`decltype(auto)` type (once deduction has run) wraps the
  // real deduced type. We never generate `AutoType`/`DeducedType` nodes, so they
  // aren't in the reverse map; peel to the deduced type so the result can be
  // sized. Unlike `typeof`/`decltype` below we don't preserve `auto` as display
  // sugar -- the deduced type (e.g. `int`, `long`, a record) is the result type.
  qt = Desugar(qt);
  if (qt.isNull())
    return {};

  // `typeof`/`decltype` sugar (`typeof (i)`, `__typeof__(i)`, `decltype(i)`)
  // wraps a resolved underlying type. None of these sugar nodes are in the
  // reverse map (we never generate them), so map the underlying type and wrap
  // it in display sugar preserving the source spelling -- mirroring
  // TypeSystemClang, whose display name for `decltype(i) j` stays `decltype(i)`
  // while the canonical type desugars to `int`.
  if (llvm::isa<clang::TypeOfExprType, clang::TypeOfType, clang::DecltypeType>(
          qt.getTypePtr())) {
    if (CompilerType inner = Convert(qt.getCanonicalType())) {
      std::string spelling = qt.getAsString(m_ast.getPrintingPolicy());
      if (!spelling.empty())
        return cpp_typesystem::Builder(m_target).CreateElaboratedType(
            spelling, inner);
      return inner;
    }
    return {};
  }

  // Types we generated (including cv-qualified variants) map back directly.
  bool found = false;
  if (CompilerType mapped = ConvertViaReverseMap(qt, found); found)
    return mapped;

  // Preserve cv-qualifiers the parser applied but that we didn't generate
  // ourselves (e.g. the `const` of the `const char *` parameter in a function
  // cast, or a `const` on a parser-defined typedef/record/builtin): map the
  // unqualified type and re-wrap it. Must run before the TypedefType/
  // BuiltinType/RecordType checks below, all of which use qt->getAs<T>() --
  // that desugars through (and so silently drops) any local qualifier.
  if (qt.hasLocalQualifiers()) {
    if (CompilerType inner = Convert(qt.getLocalUnqualifiedType()))
      return cpp_typesystem::Builder(m_target).CreateCVQualifiedType(
          inner, qt.isLocalConstQualified(), qt.isLocalVolatileQualified());
    return {};
  }

  // A typedef the parser defined itself (e.g. a persistent typedef, `typedef
  // int $bar`, written directly in the expression source) has no cpp
  // counterpart and so isn't in the reverse map. Rebuild an equivalent cpp
  // typedef aliasing the recursively-converted underlying type. Must be
  // checked before ConvertBuiltin/ConvertRecord below: qt->getAs<T>() desugars
  // through typedef sugar to find a T underneath (e.g. getAs<BuiltinType>() on
  // a `TypedefType` wrapping `int` succeeds), which would silently drop the
  // typedef and report the aliased type instead.
  if (const auto *tdt = qt->getAs<clang::TypedefType>())
    return ConvertTypedef(tdt);

  // Builtin types (int, unsigned long, bool, ...) the parser created on its own
  // -- e.g. the result type of `1 + 1`, a `sizeof` expression, or a cast -- are
  // never in the reverse map because we didn't generate them. Map them onto the
  // corresponding TypeSystemCpp builtin so the result type can be sized.
  if (const auto *bt = qt->getAs<clang::BuiltinType>()) {
    if (CompilerType builtin = ConvertBuiltin(bt))
      return builtin;
  }

  // A record type the parser defined itself (e.g. a `struct Test { ... };`
  // written directly in the expression source) has no cpp counterpart and so
  // isn't in the reverse map. Rebuild an equivalent cpp record.
  if (const auto *rt = qt->getAs<clang::RecordType>())
    return ConvertRecord(rt);

  // Simple derived types (a reference or pointer created by the parser itself,
  // e.g. the `T &` VarDecls we synthesize for locals or the result
  // synthesizer's pointer wrappers) aren't in the map. Reconstruct them.
  return ConvertDerived(qt);
}

clang::QualType ClangTypeConverter::Desugar(clang::QualType qt) {
  if (const auto *deduced = qt->getContainedDeducedType())
    return deduced->getDeducedType();
  return qt;
}

CompilerType ClangTypeConverter::ConvertViaReverseMap(clang::QualType qt,
                                                      bool &found) {
  found = false;
  auto direct = m_generator.m_reverse.find(qt.getAsOpaquePtr());
  auto find = direct;
  // A typedef the user named through a qualifier (e.g. `GlobalTypedef::V`) is
  // sugared with a nested-name-specifier, so its own opaque pointer isn't the
  // bare `TypedefType` we registered. Before falling back to the canonical type
  // (which would lose the typedef and report the aliased type, e.g. `float`),
  // rebuild the bare typedef from the same decl -- the context uniques it, so we
  // get back the exact node we registered -- and look that up.
  if (find == m_generator.m_reverse.end()) {
    if (const auto *tdt = qt->getAs<clang::TypedefType>()) {
      clang::QualType bare = m_ast.getTypedefType(
          clang::ElaboratedTypeKeyword::None, /*Qualifier=*/std::nullopt,
          tdt->getDecl());
      find = m_generator.m_reverse.find(bare.getAsOpaquePtr());
      // A TypedefType (bare, not through a nested-name-specifier) that isn't
      // itself in the map is a typedef the parser defined on its own (e.g. a
      // persistent typedef, `typedef int $bar`, or a plain `typedef int
      // MyInt;` written in the expression) and has no known cpp origin at
      // all. Falling through to the canonical-type lookup below would find
      // some unrelated cpp type that merely happens to share the same
      // canonical type (e.g. any `int` the generator already produced for
      // this expression) and silently return that instead, discarding the
      // typedef entirely. Bail out here so Convert()'s dedicated TypedefType
      // case (ConvertTypedef) rebuilds it as a proper cpp typedef instead.
      if (find == m_generator.m_reverse.end())
        return {};
    }
  }
  if (find == m_generator.m_reverse.end())
    find = m_generator.m_reverse.find(qt.getCanonicalType().getAsOpaquePtr());
  if (find == m_generator.m_reverse.end())
    return {};

  found = true;

  // If this record was only forward-declared in its own module but we
  // completed it from another module's definition, return that complete
  // definition (carrying its defining module's TypeSystemCpp) so the result
  // can be sized. This only fires for a record used by value (the one whose
  // layout was needed and thus populated the map), never for a type reached
  // solely through a pointer -- preserving lazy completion.
  if (auto redirect = m_generator.m_cross_module_complete.find(find->second);
      redirect != m_generator.m_cross_module_complete.end())
    return redirect->second;

  // Map the recovered type back into the TypeSystemCpp that actually owns it
  // (the module it was parsed from), not always m_target (the scratch
  // TypeSystemCpp that owns expression result/persistent types). A type that
  // merely passed through the expression unchanged (e.g. the plain
  // DeclRefExpr type of a local variable, or any record/field reachable from
  // it) still needs its completion state -- forward-decl-to-DIE map,
  // SymbolFile -- which lives on its owning module's TypeSystemCpp; the
  // scratch TypeSystemCpp has no SymbolFile of its own, so tagging the result
  // with it would silently make the type (and everything nested inside it,
  // e.g. a member whose own type is only discovered/completed for the first
  // time via this very access) permanently uncompletable. This does not
  // affect the lazy-completion contract: reusing the module's TypeSystemCpp
  // does not force anything complete by itself, it just makes on-demand
  // completion (e.g. GetCompleteType, or a formatter's Cast()+child access)
  // possible when something actually asks for it, exactly as it would be had
  // the value never passed through the expression evaluator. Fall back to
  // m_target for a type the generator doesn't have a recorded owner for
  // (shouldn't normally happen for anything reachable via m_reverse, but stay
  // defensive).
  TypeSystemCpp *owner = m_generator.m_type_owner.lookup(find->second);
  CompilerType mapped = (owner ? *owner : m_target).GetCompilerType(find->second);

  // The parser may have kept elaborated/spelling sugar around the type the
  // user wrote (e.g. `::Struct`, `$V< ::Struct>`). It only reached us via the
  // canonical fallback (its own opaque pointer wasn't in the map), which means
  // the spelling differs from the canonical type. Preserve that spelling as
  // pure display sugar, mirroring TypeSystemClang: the display name shows
  // `::Struct` while the canonical type name stays `Struct` so name-based
  // formatters still match.
  if (mapped && direct == m_generator.m_reverse.end() &&
      qt.getAsOpaquePtr() != qt.getCanonicalType().getAsOpaquePtr()) {
    std::string spelling = qt.getAsString(m_ast.getPrintingPolicy());
    if (!spelling.empty())
      return cpp_typesystem::Builder(m_target).CreateElaboratedType(
          spelling, mapped);
  }
  return mapped;
}

CompilerType ClangTypeConverter::ConvertBuiltin(const clang::BuiltinType *bt) {
  lldb::BasicType basic = lldb::eBasicTypeInvalid;
  switch (bt->getKind()) {
  case clang::BuiltinType::Void:
    basic = lldb::eBasicTypeVoid;
    break;
  case clang::BuiltinType::Bool:
    basic = lldb::eBasicTypeBool;
    break;
  case clang::BuiltinType::Char_U:
  case clang::BuiltinType::Char_S:
    basic = lldb::eBasicTypeChar;
    break;
  case clang::BuiltinType::UChar:
    basic = lldb::eBasicTypeUnsignedChar;
    break;
  case clang::BuiltinType::SChar:
    basic = lldb::eBasicTypeSignedChar;
    break;
  case clang::BuiltinType::WChar_U:
  case clang::BuiltinType::WChar_S:
    basic = lldb::eBasicTypeWChar;
    break;
  case clang::BuiltinType::Char8:
    basic = lldb::eBasicTypeChar8;
    break;
  case clang::BuiltinType::Char16:
    basic = lldb::eBasicTypeChar16;
    break;
  case clang::BuiltinType::Char32:
    basic = lldb::eBasicTypeChar32;
    break;
  case clang::BuiltinType::Short:
    basic = lldb::eBasicTypeShort;
    break;
  case clang::BuiltinType::UShort:
    basic = lldb::eBasicTypeUnsignedShort;
    break;
  case clang::BuiltinType::Int:
    basic = lldb::eBasicTypeInt;
    break;
  case clang::BuiltinType::UInt:
    basic = lldb::eBasicTypeUnsignedInt;
    break;
  case clang::BuiltinType::Long:
    basic = lldb::eBasicTypeLong;
    break;
  case clang::BuiltinType::ULong:
    basic = lldb::eBasicTypeUnsignedLong;
    break;
  case clang::BuiltinType::LongLong:
    basic = lldb::eBasicTypeLongLong;
    break;
  case clang::BuiltinType::ULongLong:
    basic = lldb::eBasicTypeUnsignedLongLong;
    break;
  case clang::BuiltinType::Int128:
    basic = lldb::eBasicTypeInt128;
    break;
  case clang::BuiltinType::UInt128:
    basic = lldb::eBasicTypeUnsignedInt128;
    break;
  case clang::BuiltinType::Float:
    basic = lldb::eBasicTypeFloat;
    break;
  case clang::BuiltinType::Double:
    basic = lldb::eBasicTypeDouble;
    break;
  case clang::BuiltinType::LongDouble:
    basic = lldb::eBasicTypeLongDouble;
    break;
  default:
    break;
  }
  if (basic != lldb::eBasicTypeInvalid)
    return m_target.GetBasicTypeFromAST(basic);
  return {};
}

CompilerType ClangTypeConverter::ConvertRecord(const clang::RecordType *rt) {
  clang::RecordDecl *decl = rt->getDecl();
  clang::RecordDecl *rd = decl->getDefinition();
  if (rd && rd->isCompleteDefinition()) {
    clang::QualType qt(rt, 0);
    const clang::ASTRecordLayout &layout = m_ast.getASTRecordLayout(rd);
    std::string name = rd->getNameAsString();
    cpp_typesystem::Builder builder(m_target);
    CompilerType record = builder.CreateRecordType(
        name, m_ast.getTypeSizeInChars(qt).getQuantity(),
        /*is_cpp_class=*/llvm::isa<clang::CXXRecordDecl>(rd),
        /*is_union=*/rd->isUnion());
    auto *cpp_record = llvm::cast<ct::RecordType>(
        static_cast<ct::Type *>(record.GetOpaqueQualType()));
    // Base classes (C++ records only). The parser-defined record's layout is
    // complete here, so use the concrete base offsets from it directly (unlike
    // the DWARF path, where a virtual base has no constant offset). Emit the
    // direct bases in declaration order (matching TypeSystemClang's child
    // order), tagging each with its virtuality and offset from the layout.
    auto *cpp_class = llvm::dyn_cast<ct::ClassType>(cpp_record);
    if (const auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(rd);
        cxx && cpp_class) {
      for (const clang::CXXBaseSpecifier &base : cxx->bases()) {
        const auto *base_rd = base.getType()->getAsCXXRecordDecl();
        if (!base_rd)
          continue;
        CompilerType base_type = Convert(base.getType());
        if (!base_type)
          return {};
        const bool is_virtual = base.isVirtual();
        clang::CharUnits off = is_virtual ? layout.getVBaseClassOffset(base_rd)
                                          : layout.getBaseClassOffset(base_rd);
        builder.AddBaseClass(
            *cpp_class, static_cast<ct::Type *>(base_type.GetOpaqueQualType()),
            off.getQuantity(), is_virtual);
      }
    }
    unsigned field_idx = 0;
    for (const clang::FieldDecl *fd : rd->fields()) {
      CompilerType field_type = Convert(fd->getType());
      if (!field_type)
        return {};
      uint64_t bit_offset = layout.getFieldOffset(field_idx++);
      builder.AddField(
          *cpp_record, builder.GetIdentifier(fd->getNameAsString()),
          static_cast<ct::Type *>(field_type.GetOpaqueQualType()),
          bit_offset / 8);
    }
    builder.SetRecordComplete(*cpp_record);
    SetTypeNameInfo(rd, record);
    return record;
  }
  // A record the parser only forward-declared (e.g. `struct S;` written in
  // the expression, then used through a pointer such as `S *`). It has no
  // definition and thus no layout/size, but we still need a cpp record to
  // build a `S *` pointee out of. Rebuild it as an incomplete record (no
  // byte size, no SetRecordComplete) so the pointer can be sized/stored.
  std::string name = decl->getNameAsString();
  CompilerType fwd_record = cpp_typesystem::Builder(m_target).CreateRecordType(
      name, /*byte_size=*/std::nullopt,
      /*is_cpp_class=*/llvm::isa<clang::CXXRecordDecl>(decl),
      /*is_union=*/decl->isUnion());
  SetTypeNameInfo(decl, fwd_record);
  return fwd_record;
}

CompilerType ClangTypeConverter::ConvertTypedef(
    const clang::TypedefType *tdt) {
  clang::TypedefNameDecl *decl = tdt->getDecl();
  CompilerType underlying = Convert(decl->getUnderlyingType());
  if (!underlying)
    return {};
  CompilerType result = cpp_typesystem::Builder(m_target).CreateTypedefType(
      decl->getName(), underlying);
  SetTypeNameInfo(decl, result);
  return result;
}

/// Build the interned Namespace chain enclosing \p decl_ctx (outermost
/// first), skipping any non-namespace decl context (e.g. a linkage-spec),
/// mirroring DWARFASTParserCpp::BuildDeclNamespace but walking clang
/// DeclContexts instead of DWARFDIEs.
static const cpp_typesystem::Namespace *
BuildDeclNamespace(const clang::DeclContext *decl_ctx,
                   cpp_typesystem::Builder &builder) {
  llvm::SmallVector<const clang::NamespaceDecl *, 4> namespaces;
  for (const clang::DeclContext *ctx = decl_ctx; ctx; ctx = ctx->getParent())
    if (const auto *nsd = llvm::dyn_cast<clang::NamespaceDecl>(ctx))
      namespaces.push_back(nsd);
  const cpp_typesystem::Namespace *ns = nullptr;
  for (const clang::NamespaceDecl *nsd : llvm::reverse(namespaces))
    ns = builder.GetNamespace(nsd->getName(), ns, nsd->isInline());
  return ns;
}

void ClangTypeConverter::SetTypeNameInfo(const clang::NamedDecl *decl,
                                         CompilerType type) {
  cpp_typesystem::Builder builder(m_target);
  builder.SetDeclContext(type, BuildDeclNamespace(decl->getDeclContext(),
                                                   builder));
  builder.SetUnqualifiedName(type, decl->getName());
}

CompilerType ClangTypeConverter::ConvertObjCObjectPointer(
    const clang::ObjCObjectPointerType *objc_ptr) {
  // The built-in `id` / `Class` pseudo-types (an ObjCObjectPointerType whose
  // object type has no backing interface). We generate these from the `id` /
  // `Class` typedefs (see GenerateType), so they aren't in the reverse map;
  // rebuild them as a pointer to the opaque `objc_object` / `objc_class`
  // record, matching how TypeSystemCpp models `id` (see IsPossibleDynamicType
  // / the DWARF parser).
  if (objc_ptr->isObjCIdType() || objc_ptr->isObjCClassType()) {
    cpp_typesystem::Builder builder(m_target);
    const bool is_class = objc_ptr->isObjCClassType();
    CompilerType opaque = builder.CreateRecordType(
        is_class ? "objc_class" : "objc_object",
        /*byte_size=*/std::nullopt, /*is_cpp_class=*/false,
        /*is_union=*/false);
    CompilerType ptr = builder.CreatePointerType(opaque);
    // `id` / `Class` are typedefs over `objc_object *` / `objc_class *`;
    // preserve that spelling so an expression result prints as `(id)` /
    // `(Class)` rather than the underlying `objc_object *` (matching how the
    // DWARF parser models `id` and TypeSystemClang's result type).
    return builder.CreateTypedefType(is_class ? "Class" : "id",
                                     ptr);
  }
  // A pointer to an Objective-C class (`Foo *`) the parser formed. Map the
  // pointee interface and wrap it in a cpp pointer.
  if (CompilerType pointee = Convert(objc_ptr->getPointeeType()))
    return cpp_typesystem::Builder(m_target).CreatePointerType(pointee);
  return {};
}

CompilerType ClangTypeConverter::ConvertDerived(clang::QualType qt) {
  if (const auto *objc_ptr = qt->getAs<clang::ObjCObjectPointerType>())
    return ConvertObjCObjectPointer(objc_ptr);
  if (qt->isReferenceType()) {
    if (CompilerType pointee = Convert(qt->getPointeeType()))
      return cpp_typesystem::Builder(m_target).CreateReferenceType(
          pointee, qt->isRValueReferenceType());
  } else if (qt->isPointerType()) {
    if (CompilerType pointee = Convert(qt->getPointeeType()))
      return cpp_typesystem::Builder(m_target).CreatePointerType(pointee);
  } else if (const auto *bpt = qt->getAs<clang::BlockPointerType>()) {
    // A block-literal expression (`^int(int){...}`) has a block-pointer type
    // (`int (^)(int)`). Rebuild it as a block pointer over its (function)
    // pointee so the result can be sized and stored.
    if (CompilerType pointee = Convert(bpt->getPointeeType()))
      return cpp_typesystem::Builder(m_target).CreatePointerType(
          pointee, /*is_block=*/true);
  } else if (const auto *cx = qt->getAs<clang::ComplexType>()) {
    // A complex value produced by the expression (e.g. `a + (1.0f + 2.0fi)`)
    // maps back to a TypeSystemCpp ComplexType over its mapped element.
    if (CompilerType element = Convert(cx->getElementType()))
      return cpp_typesystem::Builder(m_target).CreateComplexType(element);
  } else if (const auto *fpt = qt->getAs<clang::FunctionProtoType>()) {
    // A function type the parser formed (e.g. the pointee of a function-pointer
    // cast result). Rebuild it so a pointer to it can be sized/stored.
    CompilerType ret = Convert(fpt->getReturnType());
    cpp_typesystem::Builder builder(m_target);
    CompilerType fn = builder.CreateFunctionType(ret, fpt->isVariadic());
    for (clang::QualType param : fpt->param_types()) {
      CompilerType cpp_param = Convert(param);
      if (!cpp_param)
        return {};
      builder.AddParameter(fn, cpp_param);
    }
    return fn;
  } else if (const clang::VectorType *vt = qt->getAs<clang::VectorType>()) {
    // A vector type (e.g. an ext_vector `float __attribute__((ext_vector_type(4)))`
    // or a vector-format reinterpretation result). Rebuild it as a cpp vector
    // array so it can be sized and formatted as a vector.
    if (CompilerType element = Convert(vt->getElementType())) {
      CompilerType arr = cpp_typesystem::Builder(m_target).CreateArrayType(
          element, vt->getNumElements());
      if (auto *arr_type = llvm::dyn_cast_or_null<cpp_typesystem::ArrayType>(
              TypeSystemCpp::GetCppType(arr.GetOpaqueQualType())))
        arr_type->SetIsVector(true);
      return arr;
    }
  } else if (const clang::ArrayType *at = m_ast.getAsArrayType(qt)) {
    // An array the parser formed (e.g. the `const char16_t[6]` result type of a
    // `u"hello"` string literal). Map the element type recursively and rebuild
    // the array so the result type can be sized. A constant array carries its
    // element count; an incomplete array (`char[]`) has no bound.
    if (CompilerType element = Convert(at->getElementType())) {
      std::optional<uint64_t> num_elements;
      if (const auto *cat = llvm::dyn_cast<clang::ConstantArrayType>(at))
        num_elements = cat->getSize().getZExtValue();
      return cpp_typesystem::Builder(m_target).CreateArrayType(element,
                                                               num_elements);
    }
  }
  return {};
}

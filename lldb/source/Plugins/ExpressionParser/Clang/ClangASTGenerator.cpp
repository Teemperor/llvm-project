//===-- ClangASTGenerator.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangASTGenerator.h"

#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include "Plugins/TypeSystem/Cpp/Context.h"
#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "lldb/Core/Declaration.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
using namespace lldb_private;
using namespace lldb;
namespace ct = cpp_typesystem;

/// Records a (clang type -> cpp type) mapping so the type can later be mapped
/// back onto a TypeSystemCpp type (e.g. an expression result type).
static void noteReverse(
    llvm::DenseMap<const clang::Type *, ct::Type *> &reverse,
    const CompilerType &clang_type, ct::Type *cpp_type) {
  clang::QualType qt = ClangUtil::GetQualType(clang_type);
  if (qt.isNull())
    return;
  reverse[qt.getTypePtr()] = cpp_type;
  reverse[qt.getCanonicalType().getTypePtr()] = cpp_type;
}

CompilerType ClangASTGenerator::Generate(const CompilerType &cpp_type) {
  if (!cpp_type)
    return {};
  auto ts = cpp_type.GetTypeSystem<TypeSystemCpp>();
  if (!ts)
    return {};
  auto *type = static_cast<ct::Type *>(cpp_type.GetOpaqueQualType());
  if (!type)
    return {};
  return GenerateType(*ts, type);
}

CompilerType ClangASTGenerator::GenerateType(TypeSystemCpp &ts,
                                             ct::Type *cpp_type) {
  if (!cpp_type)
    return {};

  // Return the cached translation if we already generated this type. This also
  // breaks cycles (e.g. a record that transitively points back to itself).
  auto cached = m_generated.find(cpp_type);
  if (cached != m_generated.end())
    return m_clang_ast.GetType(
        clang::QualType::getFromOpaquePtr(cached->second));

  Log *log = GetLog(LLDBLog::Expressions);
  CompilerType result;

  if (auto *rec = llvm::dyn_cast<ct::RecordType>(cpp_type)) {
    // Records are created as forward declarations and completed on demand (see
    // CompleteRecord). This mirrors lazy DWARF parsing and keeps cycles finite.
    int kind = rec->IsUnion()
                   ? llvm::to_underlying(clang::TagTypeKind::Union)
                   : (llvm::isa<ct::ClassType>(rec)
                          ? llvm::to_underlying(clang::TagTypeKind::Class)
                          : llvm::to_underlying(clang::TagTypeKind::Struct));
    result = m_clang_ast.CreateRecordType(
        m_clang_ast.GetTranslationUnitDecl(), OptionalClangModuleID(),
        rec->GetName().GetName(), kind, eLanguageTypeC_plus_plus);
    if (result) {
      // Ask Clang to call back into us (CompleteType) before it needs the
      // definition.
      TypeSystemClang::SetHasExternalStorage(result.GetOpaqueQualType(), true);
      if (auto *rd =
              ClangUtil::GetQualType(result)->getAsRecordDecl()) {
        RecordInfo info;
        info.ts = &ts;
        info.cpp_record = rec;
        info.clang_type = result;
        m_records[rd] = info;
      }
    }
  } else if (auto *ptr = llvm::dyn_cast<ct::PointerType>(cpp_type)) {
    CompilerType pointee;
    if (ct::Type *p = ptr->GetPointeeType())
      pointee = GenerateType(ts, p);
    else
      pointee = m_clang_ast.GetBasicType(eBasicTypeVoid);
    if (pointee)
      result = pointee.GetPointerType();
  } else if (auto *ref = llvm::dyn_cast<ct::ReferenceType>(cpp_type)) {
    CompilerType pointee = GenerateType(ts, ref->GetPointeeType());
    if (pointee)
      result = ref->IsRValue()
                   ? m_clang_ast.GetRValueReferenceType(
                         pointee.GetOpaqueQualType())
                   : m_clang_ast.GetLValueReferenceType(
                         pointee.GetOpaqueQualType());
  } else if (auto *arr = llvm::dyn_cast<ct::ArrayType>(cpp_type)) {
    CompilerType elem = GenerateType(ts, arr->GetElementType());
    if (elem)
      result = m_clang_ast.CreateArrayType(elem, arr->GetNumElements(),
                                           /*is_vector=*/false);
  } else if (auto *td = llvm::dyn_cast<ct::TypedefType>(cpp_type)) {
    CompilerType underlying = GenerateType(ts, td->GetUnderlyingType());
    if (underlying)
      result = m_clang_ast.CreateTypedef(underlying.GetOpaqueQualType(),
                                         td->GetName().GetName().str().c_str(),
                                         CompilerDeclContext(), 0);
  } else if (auto *cv = llvm::dyn_cast<ct::CVQualifiedType>(cpp_type)) {
    CompilerType underlying = GenerateType(ts, cv->GetUnderlyingType());
    if (underlying) {
      if (cv->IsConst())
        underlying = m_clang_ast.AddConstModifier(underlying.GetOpaqueQualType());
      if (cv->IsVolatile())
        underlying =
            m_clang_ast.AddVolatileModifier(underlying.GetOpaqueQualType());
      result = underlying;
    }
  } else if (auto *en = llvm::dyn_cast<ct::EnumType>(cpp_type)) {
    CompilerType integer;
    if (ct::Type *ut = en->GetUnderlyingType())
      integer = GenerateType(ts, ut);
    if (!integer)
      integer = m_clang_ast.GetBuiltinTypeForEncodingAndBitSize(
          eEncodingSint, 32);
    result = m_clang_ast.CreateEnumerationType(
        en->GetName().GetName(), m_clang_ast.GetTranslationUnitDecl(),
        OptionalClangModuleID(), Declaration(), integer, en->IsScoped());
    if (result) {
      TypeSystemClang::StartTagDeclarationDefinition(result);
      const bool is_signed = en->IsSigned();
      unsigned width = 32;
      if (std::optional<uint64_t> bs = en->GetByteSize())
        width = *bs * 8;
      for (const ct::Enumerator &e : en->GetEnumerators()) {
        llvm::APSInt value(llvm::APInt(width, e.value, is_signed), !is_signed);
        m_clang_ast.AddEnumerationValueToEnumerationType(
            result, Declaration(), e.name.GetName().str().c_str(), value);
      }
      TypeSystemClang::CompleteTagDeclarationDefinition(result);
    }
  } else {
    // Builtin type.
    llvm::StringRef name = cpp_type->GetName().GetName();
    if (name == "void") {
      result = m_clang_ast.GetBasicType(eBasicTypeVoid);
    } else if (cpp_type->GetFormat() == eFormatBoolean || name == "bool") {
      result = m_clang_ast.GetBasicType(eBasicTypeBool);
    } else {
      uint64_t bit_size = 0;
      if (std::optional<uint64_t> bs = cpp_type->GetByteSize())
        bit_size = *bs * 8;
      result = m_clang_ast.GetBuiltinTypeForEncodingAndBitSize(
          cpp_type->GetEncoding(), bit_size);
    }
  }

  if (!result) {
    LLDB_LOG(log, "ClangASTGenerator: failed to translate cpp type '{0}'",
             cpp_type->GetName().GetName());
    return {};
  }

  m_generated[cpp_type] = result.GetOpaqueQualType();
  noteReverse(m_reverse, result, cpp_type);
  return result;
}

void ClangASTGenerator::PopulateRecord(clang::RecordDecl *record_decl) {
  auto it = m_records.find(record_decl);
  if (it == m_records.end())
    return;
  RecordInfo &info = it->second;
  if (info.completed)
    return;
  info.completed = true;

  TypeSystemCpp &ts = *info.ts;
  ct::RecordType *rec = info.cpp_record;

  // Make sure the record's members are parsed from debug info before we read
  // them out.
  CompilerType cpp_ct = ts.GetCompilerType(rec);
  cpp_ct.GetCompleteType();

  TypeSystemClang::StartTagDeclarationDefinition(info.clang_type);

  // Base classes (C++ classes only). A base subobject is embedded by value, so
  // Clang requires its full definition when finalizing this record; complete
  // each base before wiring it up.
  if (rec->GetNumBaseClasses()) {
    std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> bases;
    for (uint32_t i = 0; i < rec->GetNumBaseClasses(); ++i) {
      const ct::BaseClass *base = rec->GetBaseClassAtIndex(i);
      CompilerType base_ct = GenerateType(ts, base->type.Get());
      if (!base_ct)
        continue;
      EnsureComplete(base_ct);
      if (auto spec = m_clang_ast.CreateBaseClassSpecifier(
              base_ct.GetOpaqueQualType(), eAccessPublic, /*is_virtual=*/false,
              /*base_of_class=*/true))
        bases.push_back(std::move(spec));
    }
    if (!bases.empty())
      m_clang_ast.TransferBaseClasses(info.clang_type.GetOpaqueQualType(),
                                      std::move(bases));
  }

  // Fields.
  for (uint32_t i = 0; i < rec->GetNumFields(); ++i) {
    const ct::Field *field = rec->GetFieldAtIndex(i);
    CompilerType field_ct = GenerateType(ts, field->type.Get());
    if (!field_ct)
      continue;
    // A field held by value (directly or as an array element) is embedded in
    // this record, so it must be complete before we finalize the definition.
    EnsureComplete(field_ct);
    TypeSystemClang::AddFieldToRecordType(info.clang_type,
                                          field->name.GetName(), field_ct,
                                          field->bitfield_bit_size);
  }

  TypeSystemClang::CompleteTagDeclarationDefinition(info.clang_type);
}

void ClangASTGenerator::EnsureComplete(const CompilerType &clang_type) {
  clang::QualType qt = ClangUtil::GetQualType(clang_type);
  if (qt.isNull())
    return;
  // Peel array element types (embedded by value); pointers/references stop the
  // recursion because they only need a forward declaration.
  const clang::Type *type = qt.getCanonicalType().getTypePtr();
  while (const clang::ArrayType *at =
             m_clang_ast.getASTContext().getAsArrayType(clang::QualType(type, 0)))
    type = at->getElementType().getCanonicalType().getTypePtr();
  if (auto *rd = type->getAsCXXRecordDecl())
    CompleteRecord(rd);
}

bool ClangASTGenerator::CompleteRecord(clang::TagDecl *tag_decl) {
  auto *record_decl = llvm::dyn_cast<clang::RecordDecl>(tag_decl);
  if (!record_decl)
    return false;
  if (m_records.find(record_decl) == m_records.end())
    return false;
  PopulateRecord(record_decl);
  return true;
}

bool ClangASTGenerator::LayoutRecord(
    const clang::RecordDecl *record_decl, uint64_t &size, uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &vbase_offsets) {
  auto it = m_records.find(record_decl);
  if (it == m_records.end())
    return false;
  RecordInfo &info = it->second;
  ct::RecordType *rec = info.cpp_record;

  // Make sure the record is populated so we can iterate its clang fields.
  PopulateRecord(const_cast<clang::RecordDecl *>(record_decl));

  uint64_t byte_size = rec->GetByteSize().value_or(0);
  size = byte_size * 8;

  // The cpp_typesystem model doesn't carry alignment; derive a value that is
  // consistent with the record's size (Clang requires size % align == 0). For
  // the standard-layout types produced from debug info this reproduces the
  // natural alignment.
  uint64_t align_bytes = 1;
  while (align_bytes * 2 <= 8 && byte_size % (align_bytes * 2) == 0)
    align_bytes *= 2;
  alignment = align_bytes * 8;

  // Field offsets (in bits), matching the order in which we added them.
  uint32_t field_idx = 0;
  for (const clang::FieldDecl *fd : record_decl->fields()) {
    if (field_idx >= rec->GetNumFields())
      break;
    const ct::Field *field = rec->GetFieldAtIndex(field_idx++);
    uint64_t offset_bits = field->byte_offset * 8;
    if (field->IsBitfield())
      offset_bits += field->bitfield_bit_offset;
    field_offsets[fd] = offset_bits;
  }

  // Base-class offsets (in bytes), matching declaration order.
  if (const auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(record_decl)) {
    uint32_t base_idx = 0;
    for (const clang::CXXBaseSpecifier &base : cxx->bases()) {
      if (base_idx >= rec->GetNumBaseClasses())
        break;
      const ct::BaseClass *cpp_base = rec->GetBaseClassAtIndex(base_idx++);
      if (auto *base_rd = base.getType()->getAsCXXRecordDecl())
        base_offsets[base_rd] =
            clang::CharUnits::fromQuantity(cpp_base->byte_offset);
    }
  }
  (void)vbase_offsets;
  return true;
}

CompilerType ClangASTGenerator::MapClangTypeToCpp(clang::QualType qt,
                                                  TypeSystemCpp &result_ts) {
  if (qt.isNull())
    return {};
  auto find = m_reverse.find(qt.getTypePtr());
  if (find == m_reverse.end())
    find = m_reverse.find(qt.getCanonicalType().getTypePtr());
  if (find == m_reverse.end())
    return {};
  return result_ts.GetCompilerType(find->second);
}

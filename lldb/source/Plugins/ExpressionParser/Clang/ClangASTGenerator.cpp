//===-- ClangASTGenerator.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangASTGenerator.h"

#include "Plugins/TypeSystem/Cpp/Builder.h"
#include "Plugins/TypeSystem/Cpp/Context.h"
#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/StringSwitch.h"

using namespace lldb_private;
using namespace lldb;
namespace ct = cpp_typesystem;

/// Records a (clang type -> cpp type) mapping so the type can later be mapped
/// back onto a TypeSystemCpp type (e.g. an expression result type). The key is
/// the opaque QualType so cv-qualified variants (const int vs int) stay
/// distinct.
static void noteReverse(llvm::DenseMap<void *, ct::Type *> &reverse,
                        clang::QualType qt, ct::Type *cpp_type) {
  if (qt.isNull())
    return;
  reverse[qt.getAsOpaquePtr()] = cpp_type;
  reverse[qt.getCanonicalType().getAsOpaquePtr()] = cpp_type;
}

clang::QualType ClangASTGenerator::Generate(const CompilerType &cpp_type) {
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

clang::QualType ClangASTGenerator::GenerateBuiltin(ct::Type *cpp_type) {
  clang::ASTContext &ast = m_ast;
  llvm::StringRef name = cpp_type->GetName().GetName();

  // Match well-known spellings first so that same-sized types (e.g. `long` vs
  // `long long`) map to the right Clang builtin.
  clang::QualType by_name =
      llvm::StringSwitch<clang::QualType>(name)
          .Case("void", ast.VoidTy)
          .Case("bool", ast.BoolTy)
          .Case("char", ast.CharTy)
          .Case("signed char", ast.SignedCharTy)
          .Case("unsigned char", ast.UnsignedCharTy)
          .Case("wchar_t", ast.WCharTy)
          .Case("char16_t", ast.Char16Ty)
          .Case("char32_t", ast.Char32Ty)
          .Case("short", ast.ShortTy)
          .Case("short int", ast.ShortTy)
          .Case("unsigned short", ast.UnsignedShortTy)
          .Case("short unsigned int", ast.UnsignedShortTy)
          .Case("int", ast.IntTy)
          .Case("unsigned int", ast.UnsignedIntTy)
          .Case("unsigned", ast.UnsignedIntTy)
          .Case("long", ast.LongTy)
          .Case("long int", ast.LongTy)
          .Case("unsigned long", ast.UnsignedLongTy)
          .Case("long unsigned int", ast.UnsignedLongTy)
          .Case("long long", ast.LongLongTy)
          .Case("long long int", ast.LongLongTy)
          .Case("unsigned long long", ast.UnsignedLongLongTy)
          .Case("long long unsigned int", ast.UnsignedLongLongTy)
          .Case("float", ast.FloatTy)
          .Case("double", ast.DoubleTy)
          .Case("long double", ast.LongDoubleTy)
          .Case("__int128", ast.Int128Ty)
          .Case("unsigned __int128", ast.UnsignedInt128Ty)
          .Default(clang::QualType());
  if (!by_name.isNull())
    return by_name;

  // Fall back to the encoding + byte size.
  uint64_t bytes = cpp_type->GetByteSize().value_or(0);
  switch (cpp_type->GetEncoding()) {
  case eEncodingSint:
    if (bytes == 1)
      return ast.SignedCharTy;
    if (bytes == 2)
      return ast.ShortTy;
    if (bytes == 4)
      return ast.IntTy;
    if (bytes == 8)
      return ast.LongLongTy;
    break;
  case eEncodingUint:
    if (bytes == 1)
      return ast.UnsignedCharTy;
    if (bytes == 2)
      return ast.UnsignedShortTy;
    if (bytes == 4)
      return ast.UnsignedIntTy;
    if (bytes == 8)
      return ast.UnsignedLongLongTy;
    break;
  case eEncodingIEEE754:
    if (bytes == 4)
      return ast.FloatTy;
    if (bytes == 8)
      return ast.DoubleTy;
    if (bytes == 16)
      return ast.LongDoubleTy;
    break;
  default:
    break;
  }
  return {};
}

clang::QualType ClangASTGenerator::GenerateType(TypeSystemCpp &ts,
                                                ct::Type *cpp_type) {
  if (!cpp_type)
    return {};

  // Return the cached translation if we already generated this type. This also
  // breaks cycles (e.g. a record that transitively points back to itself).
  auto cached = m_generated.find(cpp_type);
  if (cached != m_generated.end())
    return clang::QualType::getFromOpaquePtr(cached->second);

  Log *log = GetLog(LLDBLog::Expressions);
  clang::ASTContext &ast = m_ast;
  clang::DeclContext *tu = ast.getTranslationUnitDecl();
  clang::QualType result;

  if (auto *rec = llvm::dyn_cast<ct::RecordType>(cpp_type)) {
    // Records are created as forward declarations and completed on demand (see
    // PopulateRecord). This mirrors lazy DWARF parsing and keeps cycles finite.
    clang::TagTypeKind kind = rec->IsUnion()
                                  ? clang::TagTypeKind::Union
                                  : (llvm::isa<ct::ClassType>(rec)
                                         ? clang::TagTypeKind::Class
                                         : clang::TagTypeKind::Struct);
    auto *decl =
        clang::CXXRecordDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    decl->setTagKind(kind);
    decl->setDeclContext(tu);
    llvm::StringRef name = rec->GetName().GetName();
    if (!name.empty())
      decl->setDeclName(&ast.Idents.get(name));
    decl->setAccess(clang::AS_public);
    tu->addDecl(decl);
    // Ask Clang to call back into us (CompleteType) before it needs the
    // definition.
    decl->setHasExternalLexicalStorage(true);
    decl->setHasExternalVisibleStorage(true);

    result = ast.getCanonicalTagType(decl);
    RecordInfo info;
    info.ts = &ts;
    info.cpp_record = rec;
    info.clang_decl = decl;
    m_records[decl] = info;
  } else if (auto *ptr = llvm::dyn_cast<ct::PointerType>(cpp_type)) {
    clang::QualType pointee;
    if (ct::Type *p = ptr->GetPointeeType())
      pointee = GenerateType(ts, p);
    else
      pointee = ast.VoidTy;
    if (!pointee.isNull())
      result = ast.getPointerType(pointee);
  } else if (auto *ref = llvm::dyn_cast<ct::ReferenceType>(cpp_type)) {
    clang::QualType pointee = GenerateType(ts, ref->GetPointeeType());
    if (!pointee.isNull())
      result = ref->IsRValue() ? ast.getRValueReferenceType(pointee)
                               : ast.getLValueReferenceType(pointee);
  } else if (auto *arr = llvm::dyn_cast<ct::ArrayType>(cpp_type)) {
    clang::QualType elem = GenerateType(ts, arr->GetElementType());
    if (!elem.isNull()) {
      if (std::optional<uint64_t> n = arr->GetNumElements())
        result = ast.getConstantArrayType(
            elem, llvm::APInt(64, *n), nullptr,
            clang::ArraySizeModifier::Normal, 0);
      else
        result = ast.getIncompleteArrayType(
            elem, clang::ArraySizeModifier::Normal, 0);
    }
  } else if (auto *td = llvm::dyn_cast<ct::TypedefType>(cpp_type)) {
    clang::QualType underlying = GenerateType(ts, td->GetUnderlyingType());
    if (!underlying.isNull()) {
      llvm::StringRef name = td->GetName().GetName();
      auto *decl = clang::TypedefDecl::Create(
          ast, tu, clang::SourceLocation(), clang::SourceLocation(),
          &ast.Idents.get(name), ast.getTrivialTypeSourceInfo(underlying));
      decl->setAccess(clang::AS_public);
      tu->addDecl(decl);
      result = ast.getTypedefType(clang::ElaboratedTypeKeyword::None,
                                  /*Qualifier=*/std::nullopt, decl);
    }
  } else if (auto *cv = llvm::dyn_cast<ct::CVQualifiedType>(cpp_type)) {
    clang::QualType underlying = GenerateType(ts, cv->GetUnderlyingType());
    if (!underlying.isNull()) {
      if (cv->IsConst())
        underlying.addConst();
      if (cv->IsVolatile())
        underlying.addVolatile();
      result = underlying;
    }
  } else if (auto *en = llvm::dyn_cast<ct::EnumType>(cpp_type)) {
    clang::QualType integer;
    if (ct::Type *ut = en->GetUnderlyingType())
      integer = GenerateType(ts, ut);
    if (integer.isNull())
      integer = ast.IntTy;

    auto *decl = clang::EnumDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    decl->setDeclContext(tu);
    llvm::StringRef name = en->GetName().GetName();
    if (!name.empty())
      decl->setDeclName(&ast.Idents.get(name));
    decl->setScoped(en->IsScoped());
    decl->setScopedUsingClassTag(en->IsScoped());
    decl->setFixed(false);
    decl->setAccess(clang::AS_public);
    tu->addDecl(decl);
    decl->startDefinition();
    decl->setIntegerType(integer);

    const bool is_signed = en->IsSigned();
    unsigned width = ast.getIntWidth(integer);
    for (const ct::Enumerator &e : en->GetEnumerators()) {
      llvm::APSInt value(llvm::APInt(width, e.value, is_signed), !is_signed);
      auto *ecd = clang::EnumConstantDecl::CreateDeserialized(
          ast, clang::GlobalDeclID());
      ecd->setDeclContext(decl);
      ecd->setDeclName(&ast.Idents.get(e.name.GetName()));
      ecd->setType(clang::QualType(integer));
      ecd->setInitVal(ast, value);
      ecd->setAccess(clang::AS_public);
      decl->addDecl(ecd);
    }

    unsigned num_negative = 0, num_positive = 0;
    ast.computeEnumBits(decl->enumerators(), num_negative, num_positive);
    clang::QualType best_type, best_promotion;
    ast.computeBestEnumTypes(/*IsPacked=*/false, num_negative, num_positive,
                             best_type, best_promotion);
    decl->completeDefinition(integer, best_promotion, num_positive,
                             num_negative);
    result = ast.getCanonicalTagType(decl);
  } else {
    result = GenerateBuiltin(cpp_type);
  }

  if (result.isNull()) {
    LLDB_LOG(log, "ClangASTGenerator: failed to translate cpp type '{0}'",
             cpp_type->GetName().GetName());
    return {};
  }

  m_generated[cpp_type] = result.getAsOpaquePtr();
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
  clang::CXXRecordDecl *decl = info.clang_decl;
  clang::ASTContext &ast = m_ast;

  // Make sure the record's members are parsed from debug info before we read
  // them out.
  CompilerType cpp_ct = ts.GetCompilerType(rec);
  cpp_ct.GetCompleteType();

  decl->startDefinition();

  // Base classes (C++ classes only). A base subobject is embedded by value, so
  // Clang requires its full definition when finalizing this record; complete
  // each base before wiring it up.
  if (rec->GetNumBaseClasses()) {
    std::vector<clang::CXXBaseSpecifier *> bases;
    for (uint32_t i = 0; i < rec->GetNumBaseClasses(); ++i) {
      const ct::BaseClass *base = rec->GetBaseClassAtIndex(i);
      clang::QualType base_qt = GenerateType(ts, base->type.Get());
      if (base_qt.isNull())
        continue;
      EnsureComplete(base_qt);
      bases.push_back(new (ast) clang::CXXBaseSpecifier(
          clang::SourceRange(), /*is_virtual=*/false, /*base_of_class=*/true,
          clang::AS_public, ast.getTrivialTypeSourceInfo(base_qt),
          clang::SourceLocation()));
    }
    if (!bases.empty())
      decl->setBases(bases.data(), bases.size());
  }

  // Fields.
  for (uint32_t i = 0; i < rec->GetNumFields(); ++i) {
    const ct::Field *field = rec->GetFieldAtIndex(i);
    clang::QualType field_qt = GenerateType(ts, field->type.Get());
    if (field_qt.isNull())
      continue;
    // A field held by value (directly or as an array element) is embedded in
    // this record, so it must be complete before we finalize the definition.
    EnsureComplete(field_qt);

    clang::Expr *bit_width = nullptr;
    if (field->IsBitfield()) {
      llvm::APInt width(ast.getIntWidth(ast.IntTy), field->bitfield_bit_size);
      clang::Expr *literal = clang::IntegerLiteral::Create(
          ast, width, ast.IntTy, clang::SourceLocation());
      bit_width = clang::ConstantExpr::Create(ast, literal,
                                              clang::APValue(llvm::APSInt(width)));
    }

    auto *fd = clang::FieldDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    fd->setDeclContext(decl);
    if (!field->name.GetName().empty())
      fd->setDeclName(&ast.Idents.get(field->name.GetName()));
    fd->setType(field_qt);
    if (bit_width)
      fd->setBitWidth(bit_width);
    fd->setAccess(clang::AS_public);
    decl->addDecl(fd);
  }

  if (!decl->isCompleteDefinition())
    decl->completeDefinition();
  decl->setHasLoadedFieldsFromExternalStorage(true);
  decl->setHasExternalLexicalStorage(false);
  decl->setHasExternalVisibleStorage(false);
}

void ClangASTGenerator::EnsureComplete(clang::QualType qt) {
  if (qt.isNull())
    return;
  // Peel array element types (embedded by value); pointers/references stop the
  // recursion because they only need a forward declaration.
  const clang::Type *type = qt.getCanonicalType().getTypePtr();
  while (const clang::ArrayType *at = m_ast.getAsArrayType(clang::QualType(type, 0)))
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

  // Types we generated (including cv-qualified variants) map back directly.
  auto find = m_reverse.find(qt.getAsOpaquePtr());
  if (find == m_reverse.end())
    find = m_reverse.find(qt.getCanonicalType().getAsOpaquePtr());
  if (find != m_reverse.end())
    return result_ts.GetCompilerType(find->second);

  // Simple derived types (a reference or pointer created by the parser itself,
  // e.g. the `T &` VarDecls we synthesize for locals or the result
  // synthesizer's pointer wrappers) aren't in the map. Reconstruct them in
  // result_ts from their pointee, which is looked up recursively.
  if (qt->isReferenceType()) {
    if (CompilerType pointee = MapClangTypeToCpp(qt->getPointeeType(), result_ts))
      return cpp_typesystem::Builder(result_ts).CreateReferenceType(
          pointee, qt->isRValueReferenceType());
  } else if (qt->isPointerType()) {
    if (CompilerType pointee = MapClangTypeToCpp(qt->getPointeeType(), result_ts))
      return cpp_typesystem::Builder(result_ts).CreatePointerType(pointee);
  }
  return {};
}

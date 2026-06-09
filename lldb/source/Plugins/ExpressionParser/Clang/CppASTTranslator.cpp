//===-- CppASTTranslator.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CppASTTranslator.h"

#include "ClangASTMetadata.h"
#include "ClangUtil.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "Plugins/TypeSystem/Clang/TypeSystemCpp.h"
#include "Plugins/TypeSystem/Clang/LLDBTypeIR.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private-enumerations.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/AST/Type.h"

using namespace lldb_private;

clang::DeclContext *
CppASTTranslator::GetOrCreateNamespaceDeclContext(LLDBNamespaceNode *ns_node) {
  if (!ns_node)
    return m_target.getASTContext().getTranslationUnitDecl();
  clang::DeclContext *parent_ctx =
      GetOrCreateNamespaceDeclContext(ns_node->parent);
  const char *name = ns_node->name.empty() ? nullptr : ns_node->name.c_str();
  return m_target.GetUniqueNamespaceDeclaration(name, parent_ctx,
                                                OptionalClangModuleID(),
                                                ns_node->is_inline);
}

CppASTTranslator::CppASTTranslator(
    TypeSystemClang &target, TypeSystemCpp *cpp_ts,
    llvm::DenseMap<LLDBTypeNode *, clang::QualType> &cache,
    std::set<const char *> &active_lookups,
    llvm::DenseSet<LLDBTypeNode *> &in_progress,
    Target *lldb_target)
    : m_target(target), m_cpp_ts(cpp_ts), m_lldb_target(lldb_target),
      m_cache(cache), m_active_lookups(active_lookups),
      m_in_progress(in_progress) {}

CompilerType CppASTTranslator::Translate(const CompilerType &src) {
  LLDBTypeNode *node = TypeSystemCpp::GetNode(src);
  if (!node)
    return {};
  LLDBQualifiers quals = TypeSystemCpp::GetQuals(src);
  clang::QualType qt = Generate(node);
  if (qt.isNull())
    return {};
  if (quals.is_const)
    qt.addConst();
  if (quals.is_volatile)
    qt.addVolatile();
  return m_target.GetType(qt);
}

clang::QualType CppASTTranslator::Generate(LLDBTypeNode *node) {
  if (!node)
    return {};
  auto it = m_cache.find(node);
  if (it != m_cache.end())
    return it->second;
  // Per-call cycle guard.
  if (!m_in_progress.insert(node).second)
    return {};
  clang::QualType qt = GenerateImpl(node);
  m_in_progress.erase(node);
  if (!qt.isNull())
    m_cache[node] = qt;
  return qt;
}

clang::QualType CppASTTranslator::GenerateImpl(LLDBTypeNode *node) {
  switch (node->kind) {
  case TypeNodeKind::Builtin: {
    auto *bt = node->As<LLDBBuiltinTypeNode>();
    CompilerType ct = m_target.GetBasicType(bt->basic_type);
    return ClangUtil::GetQualType(ct);
  }
  case TypeNodeKind::Pointer: {
    auto *pt = node->As<LLDBPointerTypeNode>();
    clang::QualType pointee = Generate(pt->pointee.node);
    if (pointee.isNull())
      pointee = m_target.getASTContext().VoidTy;
    if (pt->pointee.isConst())
      pointee = pointee.withConst();
    if (pt->pointee.isVolatile())
      pointee = pointee.withVolatile();
    return m_target.getASTContext().getPointerType(pointee);
  }
  case TypeNodeKind::LValueReference: {
    auto *rt = node->As<LLDBLValueReferenceTypeNode>();
    clang::QualType pointee = Generate(rt->pointee.node);
    if (pointee.isNull())
      return {};
    if (rt->pointee.isConst())
      pointee = pointee.withConst();
    if (rt->pointee.isVolatile())
      pointee = pointee.withVolatile();
    return m_target.getASTContext().getLValueReferenceType(pointee);
  }
  case TypeNodeKind::RValueReference: {
    auto *rt = node->As<LLDBRValueReferenceTypeNode>();
    clang::QualType pointee = Generate(rt->pointee.node);
    if (pointee.isNull())
      return {};
    if (rt->pointee.isConst())
      pointee = pointee.withConst();
    if (rt->pointee.isVolatile())
      pointee = pointee.withVolatile();
    return m_target.getASTContext().getRValueReferenceType(pointee);
  }
  case TypeNodeKind::Array: {
    auto *arr = node->As<LLDBArrayTypeNode>();
    clang::QualType elem = Generate(arr->element_type.node);
    if (elem.isNull())
      return {};
    if (arr->element_count.has_value()) {
      llvm::APInt size(64, *arr->element_count);
      return m_target.getASTContext().getConstantArrayType(
          elem, size, nullptr, clang::ArraySizeModifier::Normal, 0);
    }
    return m_target.getASTContext().getIncompleteArrayType(
        elem, clang::ArraySizeModifier::Normal, 0);
  }
  case TypeNodeKind::Record: {
    auto *rec = node->As<LLDBRecordTypeNode>();
    clang::TagTypeKind kind = rec->is_union ? clang::TagTypeKind::Union
               : rec->is_class ? clang::TagTypeKind::Class
                               : clang::TagTypeKind::Struct;

    // Determine the right DeclContext for this record before creating it.
    // For namespace-scoped records we must create them directly inside their
    // namespace so that Clang's TU lookup table never contains them; a later
    // setDeclContext() call does NOT remove the decl from the original parent's
    // lookup table, which would cause spurious ambiguity when the same
    // unqualified name exists in multiple namespaces.
    //
    // For records nested inside another record, the parent must already be in
    // the cache (we are called from the parent's nested_records generation loop
    // AFTER the parent was added to the cache), so Generate(parent) will return
    // the cached QualType without triggering recursion.
    clang::DeclContext *record_decl_ctx = nullptr;
    if (rec->parent_record_node) {
      clang::QualType parent_qt = Generate(rec->parent_record_node);
      if (!parent_qt.isNull())
        if (const auto *parent_rt = parent_qt->getAs<clang::RecordType>())
          record_decl_ctx = parent_rt->getDecl();
    } else if (rec->parent_namespace_node) {
      record_decl_ctx =
          GetOrCreateNamespaceDeclContext(rec->parent_namespace_node);
    }

    // Create the RecordDecl shell, pre-populate the cache, then trigger lazy
    // completion.  Block re-entrant lookups on the type name while
    // CreateRecordType runs (it calls addDecl which may fire
    // FindExternalVisibleDeclsByName for the name we're inserting).
    const char *name_cstr = ConstString(rec->name).GetCString();

    m_active_lookups.insert(name_cstr);
    CompilerType ct = m_target.CreateRecordType(
        record_decl_ctx, OptionalClangModuleID(), rec->name,
        static_cast<int>(kind), lldb::eLanguageTypeC_plus_plus);
    m_active_lookups.erase(name_cstr);

    clang::QualType ct_qt = ClangUtil::GetQualType(ct);
    if (!ct_qt.isNull())
      m_cache[node] = ct_qt;

    // Apply explicit alignment (e.g. alignas(N)) as an AlignedAttr so that
    // Clang's layout engine produces the correct sizeof for the type.
    if (rec->alignment_bytes > 0) {
      if (const auto *record_rt = ct_qt->getAs<clang::RecordType>()) {
        clang::ASTContext &ast_ctx = m_target.getASTContext();
        llvm::APInt align_val(64, rec->alignment_bytes);
        clang::Expr *align_expr = clang::IntegerLiteral::Create(
            ast_ctx, align_val, ast_ctx.UnsignedLongLongTy, {});
        clang::AlignedAttr *attr = clang::AlignedAttr::CreateImplicit(
            ast_ctx, /*IsAlignmentExpr=*/true, align_expr, {},
            clang::AlignedAttr::Keyword_alignas);
        attr->setCachedAlignmentValue(rec->alignment_bytes * 8);
        record_rt->getDecl()->addAttr(attr);
      }
    }

    if (!rec->is_complete && m_cpp_ts)
      m_cpp_ts->GetCompleteType((void *)node);

    // If still forcefully completed (definition not in this module), search
    // all loaded modules for a complete definition of the same type.
    if (rec->is_forcefully_completed && m_lldb_target && !rec->name.empty()) {
      // Use the qualified name if available, otherwise the simple name.
      llvm::StringRef lookup_name =
          rec->qualified_name.empty() ? rec->name : rec->qualified_name;
      TypeQuery query(lookup_name, TypeQueryOptions::e_find_one);
      TypeResults results;
      m_lldb_target->GetImages().FindTypes(nullptr, query, results);
      // Force full resolution of each found type and check if it's complete.
      results.GetTypeMap().ForEach([&](const lldb::TypeSP &type_sp) -> bool {
        if (!type_sp)
          return true;
        // Force full completion (populates fields).
        CompilerType full_ct = type_sp->GetFullCompilerType();
        auto other_ts = full_ct.GetTypeSystem<TypeSystemCpp>();
        if (!other_ts || other_ts.get() == m_cpp_ts)
          return true;
        auto *other_node = TypeSystemCpp::GetNode(full_ct);
        if (!other_node || other_node->kind != TypeNodeKind::Record)
          return true;
        auto *other_rec = other_node->As<LLDBRecordTypeNode>();
        if (!other_rec->is_complete || other_rec->is_forcefully_completed)
          return true;
        if (other_rec->name != rec->name)
          return true;
        // Found a complete definition. Copy its layout into our node.
        rec->fields = other_rec->fields;
        rec->bases = other_rec->bases;
        rec->byte_size = other_rec->byte_size;
        rec->alignment_bytes = other_rec->alignment_bytes;
        rec->is_forcefully_completed = false;
        return false; // stop iterating
      });
    }

    // Only emit a full Clang definition for genuinely complete records.
    // Forcefully-completed records are forward declarations with no definition;
    // leaving the Clang type incomplete makes IsCompleteType() return false,
    // which is the correct behavior for SBType::IsTypeComplete().
    if (rec->is_complete && !rec->is_forcefully_completed) {
      m_target.StartTagDeclarationDefinition(ct);
      {
        std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> base_specs;
        for (auto &base : rec->bases) {
          clang::QualType base_qt = Generate(base.type.node);
          if (base_qt.isNull())
            continue;
          // Skip base classes that have no Clang definition — CXXRecordDecl::
          // setBases accesses data() on each base, which asserts if the base
          // has no DefinitionData. Forcefully-completed (forward-declared)
          // bases never have a definition, so skip them.
          if (const auto *base_rt = base_qt->getAs<clang::RecordType>())
            if (!base_rt->getDecl()->isCompleteDefinition())
              continue;
          CompilerType base_ct = m_target.GetType(base_qt);
          auto spec = m_target.CreateBaseClassSpecifier(
              base_ct.GetOpaqueQualType(), base.access, base.is_virtual,
              /*base_of_class=*/true);
          if (spec)
            base_specs.push_back(std::move(spec));
        }
        if (!base_specs.empty())
          m_target.TransferBaseClasses(ct.GetOpaqueQualType(),
                                       std::move(base_specs));
      }
      uint64_t current_bit_pos = 0;
      bool prev_was_bitfield = false;
      bool has_preceding_artificial = false;
      for (auto &field : rec->fields) {
        if (field.is_artificial) {
          has_preceding_artificial = true;
          continue;
        }
        clang::QualType field_qt = Generate(field.type.node);
        if (field_qt.isNull())
          continue;
        // Skip fields with incomplete record types — computing the record
        // layout would crash when Clang tries to get the field type's size.
        if (const auto *field_rt = field_qt->getAs<clang::RecordType>())
          if (!field_rt->getDecl()->isCompleteDefinition())
            continue;
        if (field.type.isConst())
          field_qt.addConst();
        if (field.type.isVolatile())
          field_qt.addVolatile();
        CompilerType field_ct = m_target.GetType(field_qt);
        // For bitfields, insert unnamed padding if there is a gap between
        // the current bit position and where this field actually starts.
        // When transitioning from a non-bitfield to a bitfield, round up
        // to the next 32-bit word boundary first (matching Clang's unnamed
        // bitfield insertion logic in DWARFASTParserClang).
        // Exception: if current_bit_pos == 0 and a vtable pointer (artificial
        // field) or a base class occupies the beginning, Clang accounts for
        // that slot automatically — don't insert explicit padding.
        bool at_start_with_implicit_prefix =
            current_bit_pos == 0 &&
            (has_preceding_artificial || !rec->bases.empty());
        if (field.bitfield_bit_size > 0 && field.bit_offset > current_bit_pos &&
            !at_start_with_implicit_prefix) {
          uint64_t gap_start = current_bit_pos;
          if (!prev_was_bitfield && gap_start != 0 && (gap_start % 32) != 0)
            gap_start += 32 - (gap_start % 32);
          if (field.bit_offset > gap_start) {
            uint32_t gap = static_cast<uint32_t>(field.bit_offset - gap_start);
            CompilerType pad_ct = m_target.GetBuiltinTypeForEncodingAndBitSize(
                lldb::eEncodingSint, 32);
            TypeSystemClang::AddFieldToRecordType(ct, llvm::StringRef(), pad_ct,
                                                  gap);
            current_bit_pos = gap_start + gap;
          }
        }
        TypeSystemClang::AddFieldToRecordType(ct, field.name, field_ct,
                                              field.bitfield_bit_size);
        if (field.bitfield_bit_size > 0) {
          current_bit_pos = field.bit_offset + field.bitfield_bit_size;
          prev_was_bitfield = true;
        } else {
          auto size_or_err = field_ct.GetBitSize(nullptr);
          current_bit_pos = field.bit_offset +
                            (size_or_err ? *size_or_err : 0);
          prev_was_bitfield = false;
        }
      }
      for (auto &method : rec->methods) {
        if (method.is_artificial)
          continue;
        if (!method.type.node)
          continue;
        clang::QualType method_qt = Generate(method.type.node);
        if (method_qt.isNull())
          continue;
        if (method.is_const || method.is_volatile ||
            method.ref_qualifier != LLDBRefQualifier::None) {
          if (const auto *fpt =
                  llvm::dyn_cast<clang::FunctionProtoType>(method_qt)) {
            clang::FunctionProtoType::ExtProtoInfo epi = fpt->getExtProtoInfo();
            if (method.is_const)
              epi.TypeQuals.addConst();
            if (method.is_volatile)
              epi.TypeQuals.addVolatile();
            if (method.ref_qualifier == LLDBRefQualifier::LValue)
              epi.RefQualifier = clang::RQ_LValue;
            else if (method.ref_qualifier == LLDBRefQualifier::RValue)
              epi.RefQualifier = clang::RQ_RValue;
            method_qt = m_target.getASTContext().getFunctionType(
                fpt->getReturnType(), fpt->getParamTypes(), epi);
          }
        }
        CompilerType method_ct = m_target.GetType(method_qt);
        m_target.AddMethodToCXXRecordType(
            ct.GetOpaqueQualType(), method.name.c_str(), method.asm_label,
            method_ct, method.is_virtual, method.is_static,
            /*is_inline=*/false, /*is_explicit=*/false,
            /*is_attr_used=*/true, /*is_artificial=*/false);
      }
      for (auto &sm : rec->static_members) {
        if (sm.name.empty() || !sm.type.node)
          continue;
        clang::QualType sm_qt = Generate(sm.type.node);
        if (sm_qt.isNull())
          continue;
        if (sm.type.isConst())
          sm_qt.addConst();
        if (sm.type.isVolatile())
          sm_qt.addVolatile();
        CompilerType sm_ct = m_target.GetType(sm_qt);
        clang::VarDecl *var_decl =
            TypeSystemClang::AddVariableToRecordType(ct, sm.name, sm_ct);
        // If the member has an inline constant value (e.g. `const static int
        // x = 3;`), set it so Clang can fold it to a constant without a symbol
        // reference (avoids "Couldn't look up symbols" errors).
        if (var_decl && sm.const_int_value &&
            sm_qt->isIntegralOrEnumerationType()) {
          clang::ASTContext &ast_ctx = m_target.getASTContext();
          unsigned type_bits = ast_ctx.getIntWidth(var_decl->getType());
          // Skip types wider than 64 bits (e.g. __int128): DWARFFormValue
          // can only represent up to 64-bit values reliably.
          if (type_bits <= 64) {
            llvm::APSInt val = sm.const_int_value->extOrTrunc(type_bits);
            TypeSystemClang::SetIntegerInitializerForVariable(var_decl, val);
          }
        }
      }
      // Add nested typedefs (e.g. "typedef T V" inside a template).
      // Generate() returns a QualType whose TypedefDecl has this record
      // as its DeclContext; addDecl makes it findable by the expression
      // evaluator inside the struct's scope.
      if (const auto *nested_rt = ct_qt->getAs<clang::RecordType>()) {
        for (auto *td : rec->nested_typedefs) {
          clang::QualType td_qt = Generate(td);
          if (td_qt.isNull())
            continue;
          if (const auto *tdt = td_qt->getAs<clang::TypedefType>()) {
            auto *td_decl = tdt->getDecl();
            // The Typedef case may have already called addDecl (it fires when
            // the parent RecordDecl's QualType was pre-cached and the Generate
            // call in the Typedef case succeeded non-null).  Guard against the
            // double-insert that would otherwise crash in addHiddenDecl.
            if (!nested_rt->getDecl()->containsDecl(td_decl))
              nested_rt->getDecl()->addDecl(td_decl);
          }
        }
      }
      // Generate nested record types (e.g. struct Outer::Inner) so that
      // Clang can find them via qualified lookup (Outer::Inner).  We do this
      // inside the parent's definition so the nested types are part of the
      // parent's DeclContext before CompleteTagDeclarationDefinition builds
      // the lookup table.  Generate() returns {} for types already in
      // m_in_progress (cycle guard), so this is safe even during re-entrant
      // generation paths.
      for (auto *nested_rec : rec->nested_records)
        Generate(nested_rec);

      // Inject IndirectFieldDecls for anonymous struct/union members so that
      // Clang can find their fields via direct member access (e.g. obj.f when
      // f is inside an anonymous union).  Mirrors DWARFASTParserClang which
      // calls BuildIndirectFields before CompleteTagDeclarationDefinition.
      m_target.BuildIndirectFields(ct);
      // For empty C structs with byte_size == 0, Clang C++ would give size 1.
      // To preserve the C layout, add a synthetic char[0] member. In Clang's
      // C++ mode, zero-length arrays are an extension and produce sizeof == 0.
      if (rec->byte_size == 0 && rec->fields.empty() && rec->bases.empty() &&
          !rec->is_union) {
        CompilerType char_type = m_target.GetBasicType(lldb::eBasicTypeChar);
        CompilerType zero_arr = m_target.CreateArrayType(char_type, 0, false);
        TypeSystemClang::AddFieldToRecordType(ct, llvm::StringRef(), zero_arr,
                                              0);
      }
      m_target.CompleteTagDeclarationDefinition(ct);
    }
    return ct_qt;
  }
  case TypeNodeKind::Enum: {
    auto *en = node->As<LLDBEnumTypeNode>();
    // Ensure the enum is complete so we have all enumerators.
    if (!en->is_complete && m_cpp_ts)
      m_cpp_ts->GetCompleteType((void *)node);
    clang::QualType int_qt = Generate(en->integer_type.node);
    clang::ASTContext &ast_ctx = m_target.getASTContext();
    clang::TranslationUnitDecl *TU = ast_ctx.getTranslationUnitDecl();
    clang::QualType underlying_qt =
        int_qt.isNull() ? ast_ctx.IntTy : int_qt;

    // Create the EnumDecl without adding it to the TU yet, so we can
    // pre-populate m_cache before addDecl fires callbacks (which may
    // transitively need this enum type, e.g. via a struct that has it as
    // a static member type).
    clang::EnumDecl *enum_decl =
        clang::EnumDecl::CreateDeserialized(ast_ctx, clang::GlobalDeclID());
    enum_decl->setDeclContext(TU);
    if (!en->name.empty())
      enum_decl->setDeclName(&ast_ctx.Idents.get(en->name));
    enum_decl->setScoped(en->is_scoped);
    enum_decl->setScopedUsingClassTag(en->is_scoped);
    enum_decl->setFixed(false);
    enum_decl->setIntegerType(underlying_qt);
    enum_decl->setAccess(clang::AS_public);

    clang::QualType ct_qt = ast_ctx.getCanonicalTagType(enum_decl);
    // Pre-populate cache now, before addDecl triggers external-visible-decl
    // callbacks that may re-enter Generate() for this same enum node.
    if (!ct_qt.isNull())
      m_cache[node] = ct_qt;

    // Adding to TU may fire FindExternalVisibleDeclsByName; with the cache
    // populated above, those callbacks return the (incomplete) enum type
    // immediately instead of re-entering GenerateImpl.
    TU->addDecl(enum_decl);

    CompilerType ct = m_target.GetType(ct_qt);
    if (en->is_complete) {
      m_target.StartTagDeclarationDefinition(ct);
      for (auto &e : en->enumerators)
        m_target.AddEnumerationValueToEnumerationType(ct, Declaration(),
                                                      e.name.c_str(), e.value);
      m_target.CompleteTagDeclarationDefinition(ct);
    }
    return ct_qt;
  }
  case TypeNodeKind::Typedef: {
    auto *td = node->As<LLDBTypedefTypeNode>();

    clang::ASTContext &ast_ctx = m_target.getASTContext();
    clang::DeclContext *decl_ctx = ast_ctx.getTranslationUnitDecl();

    // Resolve parent context first.  If the parent is currently in
    // m_in_progress (re-entrant cycle), Generate returns null and we fall
    // back to TU — the typedef will be registered in the correct RecordDecl
    // later via the nested_typedefs loop.
    if (td->parent_node) {
      clang::QualType parent_qt = Generate(td->parent_node);
      if (!parent_qt.isNull()) {
        if (const auto *rt = parent_qt->getAs<clang::RecordType>())
          decl_ctx = rt->getDecl();
      }
    }

    clang::IdentifierInfo &id = ast_ctx.Idents.get(td->name);
    // Create the TypedefDecl with a placeholder underlying type first so we
    // can pre-populate m_cache before calling Generate(underlying_type).
    // Generate(underlying_type) may fire FindExternalVisibleDeclsByName
    // callbacks that need THIS typedef's QualType; without pre-population
    // those callbacks hit m_in_progress and return null, dropping any static
    // member whose type is this typedef.
    clang::TypeSourceInfo *placeholder_tsi =
        ast_ctx.getTrivialTypeSourceInfo(ast_ctx.IntTy);
    clang::TypedefDecl *typedef_decl = clang::TypedefDecl::Create(
        ast_ctx, decl_ctx, clang::SourceLocation{}, clang::SourceLocation{},
        &id, placeholder_tsi);
    if (llvm::isa<clang::RecordDecl>(decl_ctx))
      typedef_decl->setAccess(clang::AS_public);

    // Build the fully qualified NNS for the typedef's declared context.
    clang::NestedNameSpecifier qualifier =
        clang::TypeName::getFullyQualifiedDeclaredContext(ast_ctx, typedef_decl);
    clang::QualType result_qt = ast_ctx.getTypedefType(
        clang::ElaboratedTypeKeyword::None, qualifier, typedef_decl);

    // Pre-populate cache BEFORE resolving the underlying type so that any
    // re-entrant callbacks during Generate(underlying) find this node cached.
    if (!result_qt.isNull())
      m_cache[node] = result_qt;

    clang::QualType underlying = Generate(td->underlying_type.node);
    if (underlying.isNull()) {
      m_cache.erase(node);
      return {};
    }

    // Update the TypedefDecl with the real underlying type.
    typedef_decl->setTypeSourceInfo(
        ast_ctx.getTrivialTypeSourceInfo(underlying));

    // Register in the parent record so qualified lookup (e.g. ST::Typedef)
    // works.  If decl_ctx was resolved to a RecordDecl above, we add it now.
    // If decl_ctx fell back to TU (cycle), the nested_typedefs loop in the
    // Record case handles registration once the record is fully generated.
    if (llvm::isa<clang::RecordDecl>(decl_ctx))
      decl_ctx->addDecl(typedef_decl);

    return result_qt;
  }
  case TypeNodeKind::Function: {
    auto *fn = node->As<LLDBFunctionTypeNode>();
    clang::QualType ret = Generate(fn->return_type.node);
    if (ret.isNull())
      ret = m_target.getASTContext().VoidTy;
    llvm::SmallVector<clang::QualType, 4> params;
    for (auto &p : fn->params) {
      clang::QualType pt = Generate(p.type.node);
      if (!pt.isNull())
        params.push_back(pt);
    }
    clang::FunctionProtoType::ExtProtoInfo epi;
    if (fn->is_variadic)
      epi.Variadic = true;
    return m_target.getASTContext().getFunctionType(ret, params, epi);
  }
  case TypeNodeKind::Atomic: {
    auto *at = node->As<LLDBAtomicTypeNode>();
    clang::QualType inner = Generate(at->value_type.node);
    if (inner.isNull())
      return {};
    return m_target.getASTContext().getAtomicType(inner);
  }
  case TypeNodeKind::BlockPointer: {
    auto *bp = node->As<LLDBBlockPointerTypeNode>();
    clang::QualType fn_type = Generate(bp->function_type.node);
    if (fn_type.isNull())
      return {};
    return m_target.getASTContext().getBlockPointerType(fn_type);
  }
  case TypeNodeKind::Complex: {
    auto *cx = node->As<LLDBComplexTypeNode>();
    clang::QualType elem = Generate(cx->element_type);
    if (elem.isNull())
      return {};
    return m_target.getASTContext().getComplexType(elem);
  }
  default:
    return {};
  }
}

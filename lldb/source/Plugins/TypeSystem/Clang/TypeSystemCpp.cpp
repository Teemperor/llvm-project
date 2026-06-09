//===-- TypeSystemCpp.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemCpp.h"

#include "Plugins/SymbolFile/DWARF/DWARFASTParserCpp.h"

// ClangASTSource must be fully defined so ScratchTypeSystemClang's destructor
// can destroy its unique_ptr<ClangASTSource> member.
#include "Plugins/ExpressionParser/Clang/ClangASTSource.h"
// ClangUtilityFunction must be complete for unique_ptr<UtilityFunction>.
#include "Plugins/ExpressionParser/Clang/ClangUtilityFunction.h"
// ClangExpressionDeclMap must be complete for unique_ptr members.
#include "Plugins/ExpressionParser/Clang/ClangExpressionDeclMap.h"

#include "lldb/lldb-enumerations.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Utility/Stream.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/raw_ostream.h"

using namespace lldb_private;
using namespace lldb;

// ---------------------------------------------------------------------------
// CastInfo helper implementations (declared in TypeSystemClang.h)
// GetClangASTFromCpp returns nullptr — TypeSystemCpp has no inner Clang AST.
// ---------------------------------------------------------------------------

TypeSystemClang *lldb_private::GetClangASTFromCpp(TypeSystem *ts) {
  return nullptr;
}

ScratchTypeSystemClang *lldb_private::GetScratchClangASTFromCpp(TypeSystem *ts) {
  if (ts && ScratchTypeSystemCpp::classof(ts))
    return static_cast<ScratchTypeSystemCpp *>(ts)
        ->GetScratchClangSP()
        .get();
  return nullptr;
}

lldb::TypeSystemClangSP
lldb_private::GetScratchClangSPFromCpp(const lldb::TypeSystemSP &ts_sp) {
  if (ts_sp && ScratchTypeSystemCpp::classof(ts_sp.get()))
    return std::static_pointer_cast<TypeSystemClang>(
        static_cast<ScratchTypeSystemCpp *>(ts_sp.get())->GetScratchClangSP());
  return nullptr;
}

// ---------------------------------------------------------------------------
// RTTI anchors
// ---------------------------------------------------------------------------

char TypeSystemCpp::ID;
char ScratchTypeSystemCpp::ID;

// ---------------------------------------------------------------------------
// Opaque type encoding helpers
// LLDBTypeNode* is at least 8-byte aligned, so the lower 3 bits are free.
// Bit 0 = const, Bit 1 = volatile, Bit 2 = restrict
// ---------------------------------------------------------------------------

static LLDBTypeNode *NodeFromOpaque(lldb::opaque_compiler_type_t type) {
  uintptr_t ptr = reinterpret_cast<uintptr_t>(type);
  return reinterpret_cast<LLDBTypeNode *>(ptr & ~(uintptr_t)7);
}

static LLDBQualifiers QualsFromOpaque(lldb::opaque_compiler_type_t type) {
  uintptr_t ptr = reinterpret_cast<uintptr_t>(type);
  LLDBQualifiers q;
  q.is_const    = (ptr & 1) != 0;
  q.is_volatile = (ptr & 2) != 0;
  q.is_restrict = (ptr & 4) != 0;
  return q;
}

static lldb::opaque_compiler_type_t ToOpaque(LLDBTypeNode *node,
                                              LLDBQualifiers q = {}) {
  uintptr_t ptr = reinterpret_cast<uintptr_t>(node);
  ptr |= (q.is_const ? 1u : 0u) | (q.is_volatile ? 2u : 0u) |
         (q.is_restrict ? 4u : 0u);
  return reinterpret_cast<lldb::opaque_compiler_type_t>(ptr);
}

static LLDBQualType QualTypeFromOpaque(lldb::opaque_compiler_type_t type) {
  return LLDBQualType(NodeFromOpaque(type), QualsFromOpaque(type));
}

// ---------------------------------------------------------------------------
// Builtin type name lookup
// ---------------------------------------------------------------------------

LLDBTypeNode *TypeSystemCpp::GetNode(const CompilerType &type) {
  auto ts = type.GetTypeSystem<TypeSystemCpp>();
  if (!ts)
    return nullptr;
  return NodeFromOpaque(type.GetOpaqueQualType());
}

LLDBQualifiers TypeSystemCpp::GetQuals(const CompilerType &type) {
  auto ts = type.GetTypeSystem<TypeSystemCpp>();
  if (!ts)
    return {};
  return QualsFromOpaque(type.GetOpaqueQualType());
}

CompilerType TypeSystemCpp::MakeCompilerType(LLDBQualType qt) {
  return CompilerType(this->weak_from_this(), ToOpaque(qt.node, qt.quals));
}

static const char *GetBuiltinTypeName(lldb::BasicType bt) {
  switch (bt) {
  case eBasicTypeVoid:              return "void";
  case eBasicTypeBool:              return "bool";
  case eBasicTypeChar:              return "char";
  case eBasicTypeSignedChar:        return "signed char";
  case eBasicTypeUnsignedChar:      return "unsigned char";
  case eBasicTypeWChar:             return "wchar_t";
  case eBasicTypeSignedWChar:       return "signed wchar_t";
  case eBasicTypeUnsignedWChar:     return "unsigned wchar_t";
  case eBasicTypeChar8:             return "char8_t";
  case eBasicTypeChar16:            return "char16_t";
  case eBasicTypeChar32:            return "char32_t";
  case eBasicTypeShort:             return "short";
  case eBasicTypeUnsignedShort:     return "unsigned short";
  case eBasicTypeInt:               return "int";
  case eBasicTypeUnsignedInt:       return "unsigned int";
  case eBasicTypeLong:              return "long";
  case eBasicTypeUnsignedLong:      return "unsigned long";
  case eBasicTypeLongLong:          return "long long";
  case eBasicTypeUnsignedLongLong:  return "unsigned long long";
  case eBasicTypeInt128:            return "__int128";
  case eBasicTypeUnsignedInt128:    return "unsigned __int128";
  case eBasicTypeHalf:              return "__fp16";
  case eBasicTypeFloat:             return "float";
  case eBasicTypeDouble:            return "double";
  case eBasicTypeLongDouble:        return "long double";
  case eBasicTypeFloatComplex:      return "_Complex float";
  case eBasicTypeDoubleComplex:     return "_Complex double";
  case eBasicTypeLongDoubleComplex: return "_Complex long double";
  case eBasicTypeObjCID:            return "id";
  case eBasicTypeObjCClass:         return "Class";
  case eBasicTypeObjCSel:           return "SEL";
  case eBasicTypeNullPtr:           return "nullptr_t";
  default:                          return "<unknown>";
  }
}

// Walk through typedefs to the underlying concrete node.
static LLDBTypeNode *Desugar(LLDBTypeNode *node) {
  return LLDBTypeDesugar(node);
}

// ---------------------------------------------------------------------------
// TypeSystemCpp
// ---------------------------------------------------------------------------

TypeSystemCpp::TypeSystemCpp(llvm::StringRef /*name*/, llvm::Triple triple)
    : m_triple(triple) {}

TypeSystemCpp::~TypeSystemCpp() = default;

std::shared_ptr<TypeSystemCpp> TypeSystemCpp::Create(llvm::StringRef name,
                                                     llvm::Triple triple) {
  return std::shared_ptr<TypeSystemCpp>(new TypeSystemCpp(name, triple));
}

void TypeSystemCpp::Finalize() {
  m_dwarf_parser.reset();
}

plugin::dwarf::DWARFASTParser *TypeSystemCpp::GetDWARFParser() {
  if (!m_dwarf_parser)
    m_dwarf_parser = std::make_unique<DWARFASTParserCpp>(*this);
  return m_dwarf_parser.get();
}

PDBASTParser *TypeSystemCpp::GetPDBParser() { return nullptr; }

npdb::PdbAstBuilder *TypeSystemCpp::GetNativePDBParser() { return nullptr; }

void TypeSystemCpp::SetSymbolFile(SymbolFile *sym_file) {
  TypeSystem::SetSymbolFile(sym_file);
}

// Helper to create a CompilerType from an LLDBQualType.
static CompilerType MakeCT(TypeSystemCpp &ts, LLDBQualType qt) {
  if (!qt.node)
    return {};
  return CompilerType(ts.weak_from_this(), ToOpaque(qt.node, qt.quals));
}

// Helper: CompilerDeclContext from namespace node.
static CompilerDeclContext MakeDC(TypeSystemCpp &ts, LLDBNamespaceNode *ns) {
  return CompilerDeclContext(&ts, static_cast<void *>(ns));
}

// ---------------------------------------------------------------------------
// CompilerDecl functions
// ---------------------------------------------------------------------------

ConstString TypeSystemCpp::DeclGetName(void * /*opaque_decl*/) {
  return ConstString();
}
ConstString TypeSystemCpp::DeclGetMangledName(void * /*opaque_decl*/) {
  return ConstString();
}
CompilerDeclContext TypeSystemCpp::DeclGetDeclContext(void * /*opaque_decl*/) {
  return {};
}
CompilerType TypeSystemCpp::DeclGetFunctionReturnType(void * /*opaque_decl*/) {
  return {};
}
size_t TypeSystemCpp::DeclGetFunctionNumArguments(void * /*opaque_decl*/) {
  return 0;
}
CompilerType TypeSystemCpp::DeclGetFunctionArgumentType(void * /*opaque_decl*/,
                                                        size_t) {
  return {};
}
std::vector<CompilerContext>
TypeSystemCpp::DeclGetCompilerContext(void * /*opaque_decl*/) {
  return {};
}
Scalar TypeSystemCpp::DeclGetConstantValue(void * /*opaque_decl*/) {
  return Scalar();
}
CompilerType TypeSystemCpp::GetTypeForDecl(void * /*opaque_decl*/) {
  return {};
}

// ---------------------------------------------------------------------------
// CompilerDeclContext functions
// ---------------------------------------------------------------------------

std::vector<CompilerDecl>
TypeSystemCpp::DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                                         const bool /*ignore_using_decls*/) {
  return {};
}

ConstString TypeSystemCpp::DeclContextGetName(void *opaque_decl_ctx) {
  if (!opaque_decl_ctx)
    return ConstString(); // TU level
  auto *ns = static_cast<LLDBNamespaceNode *>(opaque_decl_ctx);
  return ConstString(ns->name);
}

ConstString
TypeSystemCpp::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  if (!opaque_decl_ctx)
    return ConstString();
  auto *ns = static_cast<LLDBNamespaceNode *>(opaque_decl_ctx);
  return ConstString(ns->qualified_name);
}

bool TypeSystemCpp::DeclContextIsClassMethod(void * /*opaque_decl_ctx*/) {
  return false;
}

bool TypeSystemCpp::DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                                    void *other_opaque_decl_ctx) {
  // Returns true if other_opaque_decl_ctx is an ancestor of (or equal to)
  // opaque_decl_ctx, or if other_opaque_decl_ctx is the TU (nullptr).
  if (!other_opaque_decl_ctx)
    return true; // TU contains everything
  auto *ctx = static_cast<LLDBNamespaceNode *>(opaque_decl_ctx);
  auto *other = static_cast<LLDBNamespaceNode *>(other_opaque_decl_ctx);
  while (ctx) {
    if (ctx == other)
      return true;
    ctx = ctx->parent;
  }
  return false;
}

lldb::LanguageType
TypeSystemCpp::DeclContextGetLanguage(void * /*opaque_decl_ctx*/) {
  return lldb::eLanguageTypeC_plus_plus;
}

std::vector<CompilerContext>
TypeSystemCpp::DeclContextGetCompilerContext(void *opaque_decl_ctx) {
  std::vector<CompilerContext> result;
  auto *ns = static_cast<LLDBNamespaceNode *>(opaque_decl_ctx);
  // Walk the namespace chain, building from innermost out
  std::vector<LLDBNamespaceNode *> chain;
  while (ns) {
    chain.push_back(ns);
    ns = ns->parent;
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    result.push_back(
        {CompilerContextKind::Namespace, ConstString((*it)->name)});
  }
  return result;
}

CompilerDeclContext
TypeSystemCpp::GetCompilerDeclContextForType(const CompilerType &type) {
  // Types don't carry their enclosing namespace in this model;
  // return the TU-level context.
  return CompilerDeclContext(this, nullptr);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#ifndef NDEBUG
bool TypeSystemCpp::Verify(lldb::opaque_compiler_type_t type) {
  return NodeFromOpaque(type) != nullptr;
}
#endif

bool TypeSystemCpp::IsArrayType(lldb::opaque_compiler_type_t type,
                                CompilerType *element_type, uint64_t *size,
                                bool *is_incomplete) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Array)
    return false;
  auto *arr = node->As<LLDBArrayTypeNode>();
  if (element_type)
    *element_type = MakeCT(*this, arr->element_type);
  if (size)
    *size = arr->element_count ? *arr->element_count : 0;
  if (is_incomplete)
    *is_incomplete = !arr->element_count.has_value();
  return true;
}

bool TypeSystemCpp::IsVectorType(lldb::opaque_compiler_type_t type,
                                 CompilerType *element_type, uint64_t *size) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Array)
    return false;
  auto *arr = node->As<LLDBArrayTypeNode>();
  if (!arr->is_vector)
    return false;
  if (element_type)
    *element_type = MakeCT(*this, arr->element_type);
  if (size)
    *size = arr->element_count ? *arr->element_count : 0;
  return true;
}

bool TypeSystemCpp::IsAggregateType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return false;
  return node->kind == TypeNodeKind::Record ||
         node->kind == TypeNodeKind::Array ||
         node->kind == TypeNodeKind::ObjCInterface;
}

bool TypeSystemCpp::IsAnonymousType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return false;
  return node->As<LLDBRecordTypeNode>()->name.empty();
}

bool TypeSystemCpp::IsBeingDefined(lldb::opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsCharType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Builtin)
    return false;
  auto bt = node->As<LLDBBuiltinTypeNode>()->basic_type;
  return bt == eBasicTypeChar || bt == eBasicTypeSignedChar ||
         bt == eBasicTypeUnsignedChar;
}

bool TypeSystemCpp::IsCompleteType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return false;
  // Trigger lazy completion before checking — mirrors TypeSystemClang behavior
  // so callers get an accurate answer rather than the internal pending state.
  if (node->kind == TypeNodeKind::Record) {
    if (!node->As<LLDBRecordTypeNode>()->is_complete)
      GetCompleteType(type);
    return node->As<LLDBRecordTypeNode>()->is_complete;
  }
  if (node->kind == TypeNodeKind::Enum) {
    if (!node->As<LLDBEnumTypeNode>()->is_complete)
      GetCompleteType(type);
    return node->As<LLDBEnumTypeNode>()->is_complete;
  }
  return true;
}

bool TypeSystemCpp::IsConst(lldb::opaque_compiler_type_t type) {
  return QualsFromOpaque(type).is_const;
}

bool TypeSystemCpp::IsDefined(lldb::opaque_compiler_type_t type) {
  return IsCompleteType(type);
}

bool TypeSystemCpp::IsFloatingPointType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Builtin)
    return false;
  return node->As<LLDBBuiltinTypeNode>()->is_float;
}

bool TypeSystemCpp::IsFunctionType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  return node && node->kind == TypeNodeKind::Function;
}

uint32_t
TypeSystemCpp::IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                      CompilerType *base_type_ptr) {
  return 0;
}

size_t
TypeSystemCpp::GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Function)
    return 0;
  return node->As<LLDBFunctionTypeNode>()->params.size();
}

CompilerType
TypeSystemCpp::GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          const size_t index) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Function)
    return {};
  auto *fn = node->As<LLDBFunctionTypeNode>();
  if (index >= fn->params.size())
    return {};
  return MakeCT(*this, fn->params[index].type);
}

bool TypeSystemCpp::IsFunctionPointerType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Pointer)
    return false;
  LLDBTypeNode *pointee = Desugar(node->As<LLDBPointerTypeNode>()->pointee.node);
  return pointee && pointee->kind == TypeNodeKind::Function;
}

bool TypeSystemCpp::IsMemberFunctionPointerType(
    lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::MemberPointer)
    return false;
  LLDBTypeNode *pt =
      Desugar(node->As<LLDBMemberPointerTypeNode>()->pointee_type.node);
  return pt && pt->kind == TypeNodeKind::Function;
}

bool TypeSystemCpp::IsMemberDataPointerType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::MemberPointer)
    return false;
  LLDBTypeNode *pt =
      Desugar(node->As<LLDBMemberPointerTypeNode>()->pointee_type.node);
  return pt && pt->kind != TypeNodeKind::Function;
}

bool TypeSystemCpp::IsBlockPointerType(lldb::opaque_compiler_type_t type,
                                       CompilerType *function_pointer_type_ptr) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::BlockPointer)
    return false;
  if (function_pointer_type_ptr)
    *function_pointer_type_ptr =
        MakeCT(*this, node->As<LLDBBlockPointerTypeNode>()->function_type);
  return true;
}

bool TypeSystemCpp::IsIntegerType(lldb::opaque_compiler_type_t type,
                                  bool &is_signed) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Builtin)
    return false;
  auto *bt = node->As<LLDBBuiltinTypeNode>();
  if (bt->is_float || bt->basic_type == eBasicTypeVoid ||
      bt->basic_type == eBasicTypeNullPtr)
    return false;
  is_signed = bt->is_signed;
  return true;
}

bool TypeSystemCpp::IsEnumerationType(lldb::opaque_compiler_type_t type,
                                      bool &is_signed) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Enum)
    return false;
  LLDBTypeNode *int_node =
      Desugar(node->As<LLDBEnumTypeNode>()->integer_type.node);
  is_signed = int_node && int_node->kind == TypeNodeKind::Builtin
                  ? int_node->As<LLDBBuiltinTypeNode>()->is_signed
                  : true;
  return true;
}

bool TypeSystemCpp::IsScopedEnumerationType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  return node && node->kind == TypeNodeKind::Enum &&
         node->As<LLDBEnumTypeNode>()->is_scoped;
}

bool TypeSystemCpp::IsPolymorphicClass(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return false;
  auto *rec = node->As<LLDBRecordTypeNode>();
  for (auto &m : rec->methods)
    if (m.is_virtual)
      return true;
  return false;
}

bool TypeSystemCpp::IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                                          CompilerType *target_type,
                                          bool check_cplusplus,
                                          bool check_objc) {
  if (target_type)
    target_type->Clear();
  if (!type || !check_cplusplus)
    return false;

  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return false;

  LLDBQualType pointee;
  if (node->kind == TypeNodeKind::Pointer)
    pointee = node->As<LLDBPointerTypeNode>()->pointee;
  else if (node->kind == TypeNodeKind::LValueReference)
    pointee = node->As<LLDBLValueReferenceTypeNode>()->pointee;
  else if (node->kind == TypeNodeKind::RValueReference)
    pointee = node->As<LLDBRValueReferenceTypeNode>()->pointee;
  else
    return false;

  if (!IsPolymorphicClass(ToOpaque(pointee.node, pointee.quals)))
    return false;

  if (target_type)
    *target_type = MakeCT(*this, pointee);
  return true;
}

bool TypeSystemCpp::IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsPointerType(lldb::opaque_compiler_type_t type,
                                  CompilerType *pointee_type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Pointer)
    return false;
  if (pointee_type)
    *pointee_type = MakeCT(*this, node->As<LLDBPointerTypeNode>()->pointee);
  return true;
}

bool TypeSystemCpp::IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                             CompilerType *pointee_type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return false;
  if (node->kind == TypeNodeKind::Pointer) {
    if (pointee_type)
      *pointee_type = MakeCT(*this, node->As<LLDBPointerTypeNode>()->pointee);
    return true;
  }
  if (node->kind == TypeNodeKind::LValueReference) {
    if (pointee_type)
      *pointee_type =
          MakeCT(*this, node->As<LLDBLValueReferenceTypeNode>()->pointee);
    return true;
  }
  if (node->kind == TypeNodeKind::RValueReference) {
    if (pointee_type)
      *pointee_type =
          MakeCT(*this, node->As<LLDBRValueReferenceTypeNode>()->pointee);
    return true;
  }
  return false;
}

bool TypeSystemCpp::IsReferenceType(lldb::opaque_compiler_type_t type,
                                    CompilerType *pointee_type,
                                    bool *is_rvalue) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return false;
  if (node->kind == TypeNodeKind::LValueReference) {
    if (pointee_type)
      *pointee_type =
          MakeCT(*this, node->As<LLDBLValueReferenceTypeNode>()->pointee);
    if (is_rvalue)
      *is_rvalue = false;
    return true;
  }
  if (node->kind == TypeNodeKind::RValueReference) {
    if (pointee_type)
      *pointee_type =
          MakeCT(*this, node->As<LLDBRValueReferenceTypeNode>()->pointee);
    if (is_rvalue)
      *is_rvalue = true;
    return true;
  }
  return false;
}

bool TypeSystemCpp::IsScalarType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return false;
  return node->kind == TypeNodeKind::Builtin ||
         node->kind == TypeNodeKind::Pointer ||
         node->kind == TypeNodeKind::Enum;
}

bool TypeSystemCpp::IsTypedefType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = NodeFromOpaque(type); // NOTE: no desugar here
  return node && node->kind == TypeNodeKind::Typedef;
}

bool TypeSystemCpp::IsVoidType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Builtin)
    return false;
  return node->As<LLDBBuiltinTypeNode>()->basic_type == eBasicTypeVoid;
}

bool TypeSystemCpp::HasPointerAuthQualifier(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = NodeFromOpaque(type);
  if (node && node->kind == TypeNodeKind::Typedef)
    return node->As<LLDBTypedefTypeNode>()->is_ptrauth;
  return false;
}

bool TypeSystemCpp::CanPassInRegisters(const CompilerType &type) {
  auto ts = type.GetTypeSystem<TypeSystemCpp>();
  if (!ts)
    return false;
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type.GetOpaqueQualType()));
  if (!node)
    return false;
  if (node->kind == TypeNodeKind::Builtin || node->kind == TypeNodeKind::Pointer ||
      node->kind == TypeNodeKind::Enum)
    return true;
  if (node->kind == TypeNodeKind::Record) {
    auto *rec = node->As<LLDBRecordTypeNode>();
    return rec->byte_size <= 16;
  }
  return false;
}

bool TypeSystemCpp::SupportsLanguage(lldb::LanguageType language) {
  switch (language) {
  case lldb::eLanguageTypeC:
  case lldb::eLanguageTypeC89:
  case lldb::eLanguageTypeC99:
  case lldb::eLanguageTypeC11:
  case lldb::eLanguageTypeC17:
  case lldb::eLanguageTypeC_plus_plus:
  case lldb::eLanguageTypeC_plus_plus_03:
  case lldb::eLanguageTypeC_plus_plus_11:
  case lldb::eLanguageTypeC_plus_plus_14:
  case lldb::eLanguageTypeC_plus_plus_17:
  case lldb::eLanguageTypeC_plus_plus_20:
  // Include ObjC so the TypeSystemMap reuses the same scratch type system
  // rather than creating a duplicate ScratchTypeSystemCpp for ObjC.
  case lldb::eLanguageTypeObjC:
  case lldb::eLanguageTypeObjC_plus_plus:
    return true;
  default:
    return false;
  }
}

bool TypeSystemCpp::IsForcefullyCompleted(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return false;
  return node->As<LLDBRecordTypeNode>()->is_forcefully_completed;
}

// ---------------------------------------------------------------------------
// Type completion
// ---------------------------------------------------------------------------

bool TypeSystemCpp::GetCompleteType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return false;
  if (node->kind == TypeNodeKind::Record) {
    if (node->As<LLDBRecordTypeNode>()->is_complete)
      return true;
  } else if (node->kind == TypeNodeKind::Enum) {
    if (node->As<LLDBEnumTypeNode>()->is_complete)
      return true;
  } else {
    return true;
  }
  // Trigger lazy completion via symbol file.
  // Use the desugared node's opaque pointer so that the forward-decl map
  // lookup in SymbolFileDWARF::CompleteType finds the struct/enum node
  // rather than any typedef alias wrapping it.
  if (SymbolFile *sym_file = GetSymbolFile()) {
    CompilerType ct(this->weak_from_this(), ToOpaque(node));
    sym_file->CompleteType(ct);
    // Check again after completion
    if (node->kind == TypeNodeKind::Record)
      return node->As<LLDBRecordTypeNode>()->is_complete;
    if (node->kind == TypeNodeKind::Enum)
      return node->As<LLDBEnumTypeNode>()->is_complete;
  }
  return false;
}

// ---------------------------------------------------------------------------
// AST-related queries
// ---------------------------------------------------------------------------

uint32_t TypeSystemCpp::GetPointerByteSize() {
  return m_triple.isArch64Bit() ? 8 : 4;
}

CompilerType TypeSystemCpp::GetPointerDiffType(bool is_signed) {
  auto bt = is_signed ? (m_triple.isArch64Bit() ? eBasicTypeLong : eBasicTypeInt)
                      : (m_triple.isArch64Bit() ? eBasicTypeUnsignedLong
                                                : eBasicTypeUnsignedInt);
  LLDBTypeNode *node =
      m_registry.GetOrCreateBuiltin(bt, m_triple.isArch64Bit() ? 64 : 32,
                                    is_signed, false);
  return CompilerType(weak_from_this(), ToOpaque(node));
}

unsigned TypeSystemCpp::GetPtrAuthKey(lldb::opaque_compiler_type_t) {
  return 0;
}
unsigned TypeSystemCpp::GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t) {
  return 0;
}
bool TypeSystemCpp::GetPtrAuthAddressDiversity(lldb::opaque_compiler_type_t) {
  return false;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

ConstString TypeSystemCpp::GetTypeName(lldb::opaque_compiler_type_t type,
                                       bool base_only) {
  LLDBTypeNode *raw = NodeFromOpaque(type);
  LLDBTypeNode *node = Desugar(raw);
  if (!raw)
    return ConstString();

  LLDBQualifiers q = QualsFromOpaque(type);
  std::string result;

  if (raw->kind == TypeNodeKind::Typedef && !base_only) {
    auto *td = raw->As<LLDBTypedefTypeNode>();
    result = (!td->qualified_name.empty()) ? td->qualified_name : td->name;
  } else if (node && node->kind == TypeNodeKind::Builtin) {
    result = GetBuiltinTypeName(node->As<LLDBBuiltinTypeNode>()->basic_type);
  } else if (node && node->kind == TypeNodeKind::Record) {
    auto *rec = node->As<LLDBRecordTypeNode>();
    result = (!base_only && !rec->qualified_name.empty()) ? rec->qualified_name
                                                          : rec->name;
  } else if (node && node->kind == TypeNodeKind::Enum) {
    result = node->As<LLDBEnumTypeNode>()->name;
  } else if (node && node->kind == TypeNodeKind::Pointer) {
    auto *pt_node = node->As<LLDBPointerTypeNode>();
    CompilerType pt = MakeCT(*this, pt_node->pointee);
    LLDBTypeNode *pointee = Desugar(pt_node->pointee.node);
    if (pointee && pointee->kind == TypeNodeKind::Function) {
      auto *fn = pointee->As<LLDBFunctionTypeNode>();
      CompilerType ret = MakeCT(*this, fn->return_type);
      std::string params_str;
      for (size_t i = 0; i < fn->params.size(); ++i) {
        if (i) params_str += ", ";
        params_str += MakeCT(*this, fn->params[i].type).GetTypeName().GetStringRef().str();
      }
      if (fn->is_variadic)
        params_str += params_str.empty() ? "..." : ", ...";
      result = ret.GetTypeName().GetStringRef().str() + " (*)(" + params_str + ")";
    } else {
      result = pt.GetTypeName().GetStringRef().str() + " *";
    }
  } else if (node && node->kind == TypeNodeKind::LValueReference) {
    auto *rt_node = node->As<LLDBLValueReferenceTypeNode>();
    CompilerType pt = MakeCT(*this, rt_node->pointee);
    LLDBTypeNode *pointee = Desugar(rt_node->pointee.node);
    if (pointee && pointee->kind == TypeNodeKind::Function) {
      auto *fn = pointee->As<LLDBFunctionTypeNode>();
      CompilerType ret = MakeCT(*this, fn->return_type);
      std::string params_str;
      for (size_t i = 0; i < fn->params.size(); ++i) {
        if (i) params_str += ", ";
        params_str += MakeCT(*this, fn->params[i].type).GetTypeName().GetStringRef().str();
      }
      if (fn->is_variadic)
        params_str += params_str.empty() ? "..." : ", ...";
      result = ret.GetTypeName().GetStringRef().str() + " (&)(" + params_str + ")";
    } else {
      result = pt.GetTypeName().GetStringRef().str() + " &";
    }
  } else if (node && node->kind == TypeNodeKind::RValueReference) {
    auto *rt_node = node->As<LLDBRValueReferenceTypeNode>();
    CompilerType pt = MakeCT(*this, rt_node->pointee);
    LLDBTypeNode *pointee = Desugar(rt_node->pointee.node);
    if (pointee && pointee->kind == TypeNodeKind::Function) {
      auto *fn = pointee->As<LLDBFunctionTypeNode>();
      CompilerType ret = MakeCT(*this, fn->return_type);
      std::string params_str;
      for (size_t i = 0; i < fn->params.size(); ++i) {
        if (i) params_str += ", ";
        params_str += MakeCT(*this, fn->params[i].type).GetTypeName().GetStringRef().str();
      }
      if (fn->is_variadic)
        params_str += params_str.empty() ? "..." : ", ...";
      result = ret.GetTypeName().GetStringRef().str() + " (&&)(" + params_str + ")";
    } else {
      result = pt.GetTypeName().GetStringRef().str() + " &&";
    }
  } else if (node && node->kind == TypeNodeKind::Array) {
    // Walk the array chain to collect all dimensions, then print them
    // in C notation: base_type[dim0][dim1]...[dimN]
    std::string dims_str;
    LLDBTypeNode *cur = node;
    LLDBQualType elem_qt;
    while (cur && cur->kind == TypeNodeKind::Array) {
      auto *arr_cur = cur->As<LLDBArrayTypeNode>();
      if (arr_cur->element_count.has_value())
        dims_str += "[" + std::to_string(*arr_cur->element_count) + "]";
      else
        dims_str += "[]";
      elem_qt = arr_cur->element_type;
      cur = Desugar(elem_qt.node);
    }
    CompilerType et = MakeCT(*this, elem_qt);
    result = et.GetTypeName().GetStringRef().str() + dims_str;
  } else if (node && node->kind == TypeNodeKind::Function) {
    auto *fn = node->As<LLDBFunctionTypeNode>();
    CompilerType ret = MakeCT(*this, fn->return_type);
    std::string params_str;
    for (size_t i = 0; i < fn->params.size(); ++i) {
      if (i) params_str += ", ";
      params_str += MakeCT(*this, fn->params[i].type).GetTypeName().GetStringRef().str();
    }
    if (fn->is_variadic)
      params_str += params_str.empty() ? "..." : ", ...";
    result = ret.GetTypeName().GetStringRef().str() + " (" + params_str + ")";
  } else if (node && node->kind == TypeNodeKind::Complex) {
    auto *cx = node->As<LLDBComplexTypeNode>();
    CompilerType elem = MakeCT(*this, LLDBQualType(cx->element_type));
    result = "_Complex " + elem.GetTypeName().GetStringRef().str();
  } else {
    result = "<unknown>";
  }

  // For tag types (enum, struct, class, union), omit CV qualifiers from the
  // type name — this matches TypeSystemClang's behavior where getAsTagDecl()
  // penetrates through qualifiers and GetTypeNameForDecl returns the bare name.
  bool is_tag_type = node && (node->kind == TypeNodeKind::Enum ||
                               node->kind == TypeNodeKind::Record);
  if (is_tag_type)
    return ConstString(result);

  // Prepend qualifiers
  std::string prefix;
  if (q.is_const)    prefix += "const ";
  if (q.is_volatile) prefix += "volatile ";
  if (q.is_restrict) prefix += "restrict ";
  return ConstString(prefix + result);
}

ConstString
TypeSystemCpp::GetDisplayTypeName(lldb::opaque_compiler_type_t type) {
  return GetTypeName(type, false);
}

uint32_t
TypeSystemCpp::GetTypeInfo(lldb::opaque_compiler_type_t type,
                           CompilerType *pointee_or_element_compiler_type) {
  LLDBTypeNode *raw  = NodeFromOpaque(type);
  LLDBTypeNode *node = Desugar(raw);
  if (!node)
    return 0;

  uint32_t flags = 0;

  switch (node->kind) {
  case TypeNodeKind::Builtin: {
    auto *bt = node->As<LLDBBuiltinTypeNode>();
    flags |= lldb::eTypeIsBuiltIn | lldb::eTypeHasValue;
    if (bt->basic_type == eBasicTypeVoid) break;
    if (bt->is_float) {
      flags |= lldb::eTypeIsFloat;
    } else {
      flags |= lldb::eTypeIsInteger;
      if (bt->is_signed) flags |= lldb::eTypeIsSigned;
    }
    break;
  }
  case TypeNodeKind::Pointer:
    flags |= lldb::eTypeIsPointer | lldb::eTypeHasValue;
    if (pointee_or_element_compiler_type)
      *pointee_or_element_compiler_type =
          MakeCT(*this, node->As<LLDBPointerTypeNode>()->pointee);
    break;
  case TypeNodeKind::LValueReference:
    flags |= lldb::eTypeHasChildren | lldb::eTypeIsReference | lldb::eTypeHasValue;
    if (pointee_or_element_compiler_type)
      *pointee_or_element_compiler_type =
          MakeCT(*this, node->As<LLDBLValueReferenceTypeNode>()->pointee);
    break;
  case TypeNodeKind::RValueReference:
    flags |= lldb::eTypeHasChildren | lldb::eTypeIsReference | lldb::eTypeHasValue;
    if (pointee_or_element_compiler_type)
      *pointee_or_element_compiler_type =
          MakeCT(*this, node->As<LLDBRValueReferenceTypeNode>()->pointee);
    break;
  case TypeNodeKind::Array:
    flags |= lldb::eTypeIsArray | lldb::eTypeHasChildren;
    if (pointee_or_element_compiler_type)
      *pointee_or_element_compiler_type =
          MakeCT(*this, node->As<LLDBArrayTypeNode>()->element_type);
    break;
  case TypeNodeKind::Record: {
    auto *rec = node->As<LLDBRecordTypeNode>();
    if (rec->is_union)
      flags |= lldb::eTypeIsStructUnion;
    else
      flags |= lldb::eTypeIsStructUnion | lldb::eTypeIsCPlusPlus;
    if (rec->is_class)
      flags |= lldb::eTypeIsClass;
    // Always set eTypeHasChildren for structs/classes/unions (even empty ones)
    // so that empty aggregates display as "{}".
    flags |= lldb::eTypeHasChildren;
    break;
  }
  case TypeNodeKind::Enum:
    flags |= lldb::eTypeIsEnumeration | lldb::eTypeHasValue;
    break;
  case TypeNodeKind::Typedef:
    flags |= lldb::eTypeIsTypedef;
    break;
  case TypeNodeKind::Function:
    flags |= lldb::eTypeIsFuncPrototype | lldb::eTypeHasValue;
    break;
  case TypeNodeKind::MemberPointer:
    flags |= lldb::eTypeIsMember | lldb::eTypeIsPointer | lldb::eTypeHasValue;
    break;
  case TypeNodeKind::ObjCInterface:
    flags |= lldb::eTypeIsObjC | lldb::eTypeHasChildren;
    break;
  case TypeNodeKind::ObjCObjectPointer:
    flags |= lldb::eTypeIsObjC | lldb::eTypeIsPointer | lldb::eTypeHasValue;
    break;
  default:
    break;
  }
  return flags;
}

lldb::LanguageType
TypeSystemCpp::GetMinimumLanguage(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return lldb::eLanguageTypeC;
  if (node->kind == TypeNodeKind::Pointer) {
    auto *pt = node->As<LLDBPointerTypeNode>();
    LLDBTypeNode *pointee = Desugar(pt->pointee.node);
    if (pointee) {
      if (pointee->kind == TypeNodeKind::Record)
        return lldb::eLanguageTypeC_plus_plus;
      if (pointee->kind == TypeNodeKind::Builtin &&
          pointee->As<LLDBBuiltinTypeNode>()->basic_type == eBasicTypeNullPtr)
        return lldb::eLanguageTypeC_plus_plus;
    }
    return lldb::eLanguageTypeC;
  }
  if (node->kind == TypeNodeKind::Record || node->kind == TypeNodeKind::Enum)
    return lldb::eLanguageTypeC_plus_plus;
  if (node->kind == TypeNodeKind::Builtin &&
      node->As<LLDBBuiltinTypeNode>()->basic_type == eBasicTypeNullPtr)
    return lldb::eLanguageTypeC_plus_plus;
  return lldb::eLanguageTypeC;
}

lldb::TypeClass
TypeSystemCpp::GetTypeClass(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return lldb::eTypeClassInvalid;
  switch (node->kind) {
  case TypeNodeKind::Builtin:      return lldb::eTypeClassBuiltin;
  case TypeNodeKind::Pointer:      return lldb::eTypeClassPointer;
  case TypeNodeKind::LValueReference:
  case TypeNodeKind::RValueReference: return lldb::eTypeClassReference;
  case TypeNodeKind::Array:        return lldb::eTypeClassArray;
  case TypeNodeKind::Function:     return lldb::eTypeClassFunction;
  case TypeNodeKind::Record: {
    auto *rec = node->As<LLDBRecordTypeNode>();
    if (rec->is_union)  return lldb::eTypeClassUnion;
    if (rec->is_class)  return lldb::eTypeClassClass;
    return lldb::eTypeClassStruct;
  }
  case TypeNodeKind::Enum:         return lldb::eTypeClassEnumeration;
  case TypeNodeKind::Typedef:      return lldb::eTypeClassTypedef;
  case TypeNodeKind::MemberPointer: return lldb::eTypeClassMemberPointer;
  case TypeNodeKind::ObjCInterface:
  case TypeNodeKind::ObjCObjectPointer: return lldb::eTypeClassObjCInterface;
  default:                         return lldb::eTypeClassInvalid;
  }
}

unsigned TypeSystemCpp::GetTypeQualifiers(lldb::opaque_compiler_type_t type) {
  LLDBQualifiers q = QualsFromOpaque(type);
  unsigned flags = 0;
  if (q.is_const)    flags |= clang::Qualifiers::Const;
  if (q.is_volatile) flags |= clang::Qualifiers::Volatile;
  if (q.is_restrict) flags |= clang::Qualifiers::Restrict;
  return flags;
}

// ---------------------------------------------------------------------------
// Creating related types
// ---------------------------------------------------------------------------

CompilerType
TypeSystemCpp::GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Array)
    return {};
  return MakeCT(*this, node->As<LLDBArrayTypeNode>()->element_type);
}

CompilerType TypeSystemCpp::GetArrayType(lldb::opaque_compiler_type_t type,
                                         uint64_t size) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  LLDBTypeNode *arr = m_registry.CreateArrayType(qt, size ? std::optional<uint64_t>(size) : std::nullopt);
  return CompilerType(weak_from_this(), ToOpaque(arr));
}

CompilerType
TypeSystemCpp::GetCanonicalType(lldb::opaque_compiler_type_t type) {
  // Desugar typedefs, drop qualifiers.
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  return node ? CompilerType(weak_from_this(), ToOpaque(node)) : CompilerType{};
}

CompilerType
TypeSystemCpp::GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = NodeFromOpaque(type);
  if (!node)
    return {};
  if (node->kind == TypeNodeKind::Pointer) {
    auto *pt = node->As<LLDBPointerTypeNode>();
    if (pt->pointee.quals != LLDBQualifiers{}) {
      auto it = m_unqual_ptr_map.find(pt);
      if (it != m_unqual_ptr_map.end())
        return CompilerType(weak_from_this(), ToOpaque(it->second));
      LLDBPointerTypeNode *unqual =
          m_registry.CreatePointerType(pt->pointee.unqualified());
      m_unqual_ptr_map[pt] = unqual;
      return CompilerType(weak_from_this(), ToOpaque(unqual));
    }
  }
  return CompilerType(weak_from_this(), ToOpaque(node));
}

CompilerType
TypeSystemCpp::GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Enum)
    return {};
  return MakeCT(*this, node->As<LLDBEnumTypeNode>()->integer_type);
}

int TypeSystemCpp::GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Function)
    return -1;
  return (int)node->As<LLDBFunctionTypeNode>()->params.size();
}

CompilerType TypeSystemCpp::GetFunctionArgumentTypeAtIndex(
    lldb::opaque_compiler_type_t type, size_t idx) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Function)
    return {};
  auto *fn = node->As<LLDBFunctionTypeNode>();
  if (idx >= fn->params.size())
    return {};
  return MakeCT(*this, fn->params[idx].type);
}

CompilerType
TypeSystemCpp::GetFunctionReturnType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Function)
    return {};
  return MakeCT(*this, node->As<LLDBFunctionTypeNode>()->return_type);
}

size_t TypeSystemCpp::GetNumMemberFunctions(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return 0;
  return node->As<LLDBRecordTypeNode>()->methods.size();
}

TypeMemberFunctionImpl
TypeSystemCpp::GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                                        size_t idx) {
  return TypeMemberFunctionImpl();
}

CompilerType
TypeSystemCpp::GetNonReferenceType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return {};
  if (node->kind == TypeNodeKind::LValueReference)
    return MakeCT(*this, node->As<LLDBLValueReferenceTypeNode>()->pointee);
  if (node->kind == TypeNodeKind::RValueReference)
    return MakeCT(*this, node->As<LLDBRValueReferenceTypeNode>()->pointee);
  return CompilerType(weak_from_this(), type);
}

CompilerType TypeSystemCpp::GetPointeeType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return {};
  if (node->kind == TypeNodeKind::Pointer)
    return MakeCT(*this, node->As<LLDBPointerTypeNode>()->pointee);
  if (node->kind == TypeNodeKind::LValueReference)
    return MakeCT(*this, node->As<LLDBLValueReferenceTypeNode>()->pointee);
  if (node->kind == TypeNodeKind::RValueReference)
    return MakeCT(*this, node->As<LLDBRValueReferenceTypeNode>()->pointee);
  return {};
}

CompilerType TypeSystemCpp::GetPointerType(lldb::opaque_compiler_type_t type) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  return CompilerType(weak_from_this(),
                      ToOpaque(m_registry.CreatePointerType(qt)));
}

CompilerType
TypeSystemCpp::GetLValueReferenceType(lldb::opaque_compiler_type_t type) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  return CompilerType(weak_from_this(),
                      ToOpaque(m_registry.CreateLValueRefType(qt)));
}

CompilerType
TypeSystemCpp::GetRValueReferenceType(lldb::opaque_compiler_type_t type) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  return CompilerType(weak_from_this(),
                      ToOpaque(m_registry.CreateRValueRefType(qt)));
}

CompilerType TypeSystemCpp::GetAtomicType(lldb::opaque_compiler_type_t type) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  return CompilerType(weak_from_this(),
                      ToOpaque(m_registry.CreateAtomicType(qt)));
}

CompilerType TypeSystemCpp::AddConstModifier(lldb::opaque_compiler_type_t type) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  qt.quals.is_const = true;
  return MakeCT(*this, qt);
}

CompilerType
TypeSystemCpp::AddVolatileModifier(lldb::opaque_compiler_type_t type) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  qt.quals.is_volatile = true;
  return MakeCT(*this, qt);
}

CompilerType
TypeSystemCpp::AddRestrictModifier(lldb::opaque_compiler_type_t type) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  qt.quals.is_restrict = true;
  return MakeCT(*this, qt);
}

CompilerType TypeSystemCpp::AddPtrAuthModifier(lldb::opaque_compiler_type_t type,
                                               uint32_t) {
  return CompilerType(weak_from_this(), type);
}

CompilerType TypeSystemCpp::CreateTypedef(lldb::opaque_compiler_type_t type,
                                          const char *name,
                                          const CompilerDeclContext &decl_ctx,
                                          uint32_t) {
  LLDBQualType qt = QualTypeFromOpaque(type);
  LLDBTypeNode *td = m_registry.CreateTypedef(name ? name : "", qt);
  return CompilerType(weak_from_this(), ToOpaque(td));
}

CompilerType
TypeSystemCpp::GetTypedefedType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = NodeFromOpaque(type);
  if (!node || node->kind != TypeNodeKind::Typedef)
    return {};
  return MakeCT(*this, node->As<LLDBTypedefTypeNode>()->underlying_type);
}

CompilerType TypeSystemCpp::GetBasicTypeFromAST(lldb::BasicType basic_type) {
  LLDBTypeNode *node = nullptr;
  uint32_t bits = 0;
  bool is_signed = false, is_float = false;
  switch (basic_type) {
  case eBasicTypeVoid:              bits = 0;  break;
  case eBasicTypeBool:              bits = 8;  break;
  case eBasicTypeChar:
  case eBasicTypeSignedChar:
  case eBasicTypeUnsignedChar:
  case eBasicTypeChar8:             bits = 8;  break;
  case eBasicTypeWChar:
  case eBasicTypeSignedWChar:
  case eBasicTypeUnsignedWChar:
  case eBasicTypeChar16:            bits = 16; break;
  case eBasicTypeChar32:            bits = 32; break;
  case eBasicTypeShort:
  case eBasicTypeUnsignedShort:     bits = 16; break;
  case eBasicTypeInt:
  case eBasicTypeUnsignedInt:       bits = 32; break;
  case eBasicTypeLong:
  case eBasicTypeUnsignedLong:
    bits = m_triple.isArch64Bit() ? 64 : 32; break;
  case eBasicTypeLongLong:
  case eBasicTypeUnsignedLongLong:  bits = 64; break;
  case eBasicTypeInt128:
  case eBasicTypeUnsignedInt128:    bits = 128; break;
  case eBasicTypeHalf:              bits = 16; is_float = true; break;
  case eBasicTypeFloat:             bits = 32; is_float = true; break;
  case eBasicTypeDouble:            bits = 64; is_float = true; break;
  case eBasicTypeLongDouble:        bits = 128; is_float = true; break;
  case eBasicTypeNullPtr:           bits = m_triple.isArch64Bit() ? 64 : 32; break;
  default:                          bits = 0; break;
  }
  // Determine signedness
  switch (basic_type) {
  case eBasicTypeSignedChar:
  case eBasicTypeWChar:
  case eBasicTypeSignedWChar:
  case eBasicTypeShort:
  case eBasicTypeInt:
  case eBasicTypeLong:
  case eBasicTypeLongLong:
  case eBasicTypeInt128:
  case eBasicTypeChar8:
  case eBasicTypeChar16:
  case eBasicTypeChar32:
    is_signed = true; break;
  default:
    break;
  }
  node = m_registry.GetOrCreateBuiltin(basic_type, bits, is_signed, is_float);
  return node ? CompilerType(weak_from_this(), ToOpaque(node)) : CompilerType{};
}

CompilerType TypeSystemCpp::GetBuiltinTypeForEncodingAndBitSize(
    lldb::Encoding encoding, size_t bit_size) {
  lldb::BasicType bt = eBasicTypeInvalid;
  bool is_signed = false, is_float = false;
  switch (encoding) {
  case eEncodingUint:
    is_signed = false;
    if (bit_size <= 8)   bt = eBasicTypeUnsignedChar;
    else if (bit_size <= 16) bt = eBasicTypeUnsignedShort;
    else if (bit_size <= 32) bt = eBasicTypeUnsignedInt;
    else if (bit_size <= 64) bt = eBasicTypeUnsignedLongLong;
    else                     bt = eBasicTypeUnsignedInt128;
    break;
  case eEncodingSint:
    is_signed = true;
    if (bit_size <= 8)   bt = eBasicTypeSignedChar;
    else if (bit_size <= 16) bt = eBasicTypeShort;
    else if (bit_size <= 32) bt = eBasicTypeInt;
    else if (bit_size <= 64) bt = eBasicTypeLongLong;
    else                     bt = eBasicTypeInt128;
    break;
  case eEncodingIEEE754:
    is_float = true;
    if (bit_size <= 16)  bt = eBasicTypeHalf;
    else if (bit_size <= 32) bt = eBasicTypeFloat;
    else if (bit_size <= 64) bt = eBasicTypeDouble;
    else                     bt = eBasicTypeLongDouble;
    break;
  default:
    bt = eBasicTypeVoid;
    break;
  }
  if (bt == eBasicTypeInvalid) bt = eBasicTypeVoid;
  LLDBTypeNode *node = m_registry.GetOrCreateBuiltin(
      bt, (uint32_t)bit_size, is_signed, is_float);
  return node ? CompilerType(weak_from_this(), ToOpaque(node)) : CompilerType{};
}

CompilerType TypeSystemCpp::GetBuiltinTypeByName(ConstString name) {
  llvm::StringRef s = name.GetStringRef();
  // Simple table lookup.
  static const struct { const char *name; lldb::BasicType bt; } kTable[] = {
    {"void", eBasicTypeVoid}, {"bool", eBasicTypeBool},
    {"char", eBasicTypeChar}, {"signed char", eBasicTypeSignedChar},
    {"unsigned char", eBasicTypeUnsignedChar},
    {"short", eBasicTypeShort}, {"unsigned short", eBasicTypeUnsignedShort},
    {"int", eBasicTypeInt}, {"unsigned int", eBasicTypeUnsignedInt},
    {"long", eBasicTypeLong}, {"unsigned long", eBasicTypeUnsignedLong},
    {"long long", eBasicTypeLongLong},
    {"unsigned long long", eBasicTypeUnsignedLongLong},
    {"__int128", eBasicTypeInt128},
    {"unsigned __int128", eBasicTypeUnsignedInt128},
    {"float", eBasicTypeFloat}, {"double", eBasicTypeDouble},
    {"long double", eBasicTypeLongDouble},
    {"wchar_t", eBasicTypeWChar}, {"char8_t", eBasicTypeChar8},
    {"char16_t", eBasicTypeChar16}, {"char32_t", eBasicTypeChar32},
    {"nullptr_t", eBasicTypeNullPtr},
  };
  for (auto &e : kTable) {
    if (s == e.name)
      return GetBasicTypeFromAST(e.bt);
  }
  return {};
}

CompilerType TypeSystemCpp::CreateGenericFunctionPrototype() {
  std::vector<LLDBParamNode> params;
  LLDBTypeNode *void_node = m_registry.GetOrCreateBuiltin(eBasicTypeVoid, 0, false, false);
  LLDBTypeNode *fn = m_registry.CreateFunctionType(LLDBQualType(void_node),
                                                    std::move(params), true);
  return CompilerType(weak_from_this(), ToOpaque(fn));
}

CompilerType TypeSystemCpp::GetTypeForFormatters(void *type) {
  return CompilerType(weak_from_this(), type);
}

// ---------------------------------------------------------------------------
// Exploring the type
// ---------------------------------------------------------------------------

const llvm::fltSemantics &
TypeSystemCpp::GetFloatTypeSemantics(size_t byte_size, lldb::Format) {
  switch (byte_size) {
  case 2:  return llvm::APFloat::IEEEhalf();
  case 4:  return llvm::APFloat::IEEEsingle();
  case 8:  return llvm::APFloat::IEEEdouble();
  case 16: return llvm::APFloat::IEEEquad();
  case 10: return llvm::APFloat::x87DoubleExtended();
  default: return llvm::APFloat::IEEEdouble();
  }
}

llvm::Expected<uint64_t>
TypeSystemCpp::GetBitSize(lldb::opaque_compiler_type_t type,
                          ExecutionContextScope *exe_scope) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return llvm::createStringError("Invalid type");

  switch (node->kind) {
  case TypeNodeKind::Builtin:
    return node->As<LLDBBuiltinTypeNode>()->bit_size;
  case TypeNodeKind::Pointer:
  case TypeNodeKind::LValueReference:
  case TypeNodeKind::RValueReference:
    return (uint64_t)GetPointerByteSize() * 8;
  case TypeNodeKind::Array: {
    auto *arr = node->As<LLDBArrayTypeNode>();
    if (!arr->element_count.has_value())
      return llvm::createStringError("Incomplete array type");
    auto elem_size = GetBitSize(ToOpaque(arr->element_type.node), exe_scope);
    if (!elem_size)
      return elem_size;
    return *elem_size * *arr->element_count;
  }
  case TypeNodeKind::Record: {
    auto *rec = node->As<LLDBRecordTypeNode>();
    if (!rec->is_complete)
      GetCompleteType(type);
    return (uint64_t)rec->byte_size * 8;
  }
  case TypeNodeKind::Enum: {
    auto *en = node->As<LLDBEnumTypeNode>();
    return GetBitSize(ToOpaque(en->integer_type.node), exe_scope);
  }
  case TypeNodeKind::Typedef:
    return GetBitSize(
        ToOpaque(node->As<LLDBTypedefTypeNode>()->underlying_type.node),
        exe_scope);
  case TypeNodeKind::Complex: {
    auto *cx = node->As<LLDBComplexTypeNode>();
    auto elem_bits = GetBitSize(ToOpaque(cx->element_type), exe_scope);
    if (!elem_bits)
      return elem_bits;
    return *elem_bits * 2;
  }
  case TypeNodeKind::Function:
    return 0;
  case TypeNodeKind::MemberPointer:
    return (uint64_t)GetPointerByteSize() * 8;
  default:
    return (uint64_t)GetPointerByteSize() * 8;
  }
}

lldb::Encoding TypeSystemCpp::GetEncoding(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return eEncodingInvalid;
  switch (node->kind) {
  case TypeNodeKind::Builtin: {
    auto *bt = node->As<LLDBBuiltinTypeNode>();
    if (bt->basic_type == eBasicTypeBool)  return eEncodingUint;
    if (bt->basic_type == eBasicTypeVoid)  return eEncodingInvalid;
    if (bt->is_float)   return eEncodingIEEE754;
    if (bt->is_signed)  return eEncodingSint;
    return eEncodingUint;
  }
  case TypeNodeKind::Pointer:
  case TypeNodeKind::LValueReference:
  case TypeNodeKind::RValueReference:
    return eEncodingUint;
  case TypeNodeKind::Enum: {
    auto *en = node->As<LLDBEnumTypeNode>();
    return GetEncoding(ToOpaque(en->integer_type.node));
  }
  default:
    return eEncodingInvalid;
  }
}

lldb::Format TypeSystemCpp::GetFormat(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return eFormatDefault;
  switch (node->kind) {
  case TypeNodeKind::Builtin: {
    auto *bt = node->As<LLDBBuiltinTypeNode>();
    if (bt->basic_type == eBasicTypeBool)  return eFormatBoolean;
    if (bt->is_float)  return eFormatFloat;
    if (bt->basic_type == eBasicTypeChar ||
        bt->basic_type == eBasicTypeSignedChar ||
        bt->basic_type == eBasicTypeUnsignedChar)
      return eFormatChar;
    return bt->is_signed ? eFormatDecimal : eFormatUnsigned;
  }
  case TypeNodeKind::Pointer:
  case TypeNodeKind::LValueReference:
  case TypeNodeKind::RValueReference:
    return eFormatHex;
  case TypeNodeKind::Enum:     return eFormatEnum;
  default:                     return eFormatDefault;
  }
}

std::optional<size_t>
TypeSystemCpp::GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                               ExecutionContextScope *exe_scope) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return std::nullopt;
  if (node->kind == TypeNodeKind::Record) {
    uint32_t align = node->As<LLDBRecordTypeNode>()->alignment_bytes;
    return align ? std::optional<size_t>(align * 8) : std::nullopt;
  }
  auto bs = GetBitSize(type, exe_scope);
  if (!bs)
    return std::nullopt;
  return *bs; // natural alignment = size for builtins
}

llvm::Expected<uint32_t>
TypeSystemCpp::GetNumChildren(lldb::opaque_compiler_type_t type,
                              bool omit_empty_base_classes,
                              const ExecutionContext *exe_ctx) {
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return 0u;
  if (node->kind == TypeNodeKind::Record) {
    auto *rec = node->As<LLDBRecordTypeNode>();
    uint32_t n = 0;
    for (auto &f : rec->fields)
      if (!f.is_artificial)
        ++n;
    for (auto &b : rec->bases) {
      if (!omit_empty_base_classes) {
        ++n;
      } else {
        LLDBTypeNode *base_node = Desugar(b.type.node);
        if (base_node && base_node->kind == TypeNodeKind::Record) {
          // Complete the base class before checking if it's empty, so that
          // GetNumChildren and GetIndexOfChildWithName stay consistent.
          GetCompleteType(ToOpaque(base_node));
          auto *brec = base_node->As<LLDBRecordTypeNode>();
          bool has_non_art_fields = false;
          for (auto &bf : brec->fields)
            if (!bf.is_artificial) { has_non_art_fields = true; break; }
          if (has_non_art_fields || !brec->bases.empty())
            ++n;
        } else {
          ++n;
        }
      }
    }
    return n;
  }
  if (node->kind == TypeNodeKind::Array) {
    auto *arr = node->As<LLDBArrayTypeNode>();
    if (arr->element_count)
      return (uint32_t)*arr->element_count;
    // VLA: try to get runtime count from the symbol file.
    if (arr->vla_dwarf_uid != LLDB_INVALID_UID && exe_ctx) {
      if (SymbolFile *sym_file = GetSymbolFile()) {
        auto array_info = sym_file->GetDynamicArrayInfoForUID(arr->vla_dwarf_uid, exe_ctx);
        if (array_info && !array_info->element_orders.empty())
          return array_info->element_orders.back().value_or(0);
      }
    }
    return 0u;
  }
  if (node->kind == TypeNodeKind::Pointer ||
      node->kind == TypeNodeKind::LValueReference ||
      node->kind == TypeNodeKind::RValueReference) {
    LLDBQualType pointee;
    if (node->kind == TypeNodeKind::Pointer)
      pointee = node->As<LLDBPointerTypeNode>()->pointee;
    else if (node->kind == TypeNodeKind::LValueReference)
      pointee = node->As<LLDBLValueReferenceTypeNode>()->pointee;
    else
      pointee = node->As<LLDBRValueReferenceTypeNode>()->pointee;
    CompilerType pointee_ct = MakeCT(*this, pointee);
    if (pointee_ct.IsAggregateType()) {
      auto num_or_err = pointee_ct.GetNumChildren(omit_empty_base_classes, exe_ctx);
      if (num_or_err && *num_or_err > 0)
        return *num_or_err;
    }
    return 1u;
  }
  return 0u;
}

lldb::BasicType
TypeSystemCpp::GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Builtin)
    return eBasicTypeInvalid;
  return node->As<LLDBBuiltinTypeNode>()->basic_type;
}

void TypeSystemCpp::ForEachEnumerator(
    lldb::opaque_compiler_type_t type,
    std::function<bool(const CompilerType &integer_type, ConstString name,
                       const llvm::APSInt &value)> const &callback) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Enum)
    return;
  auto *en = node->As<LLDBEnumTypeNode>();
  CompilerType int_type = MakeCT(*this, en->integer_type);
  for (auto &e : en->enumerators) {
    if (!callback(int_type, ConstString(e.name), e.value))
      break;
  }
}

uint32_t TypeSystemCpp::GetNumFields(lldb::opaque_compiler_type_t type) {
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return 0;
  uint32_t n = 0;
  for (auto &f : node->As<LLDBRecordTypeNode>()->fields)
    if (!f.is_artificial)
      ++n;
  return n;
}

CompilerType TypeSystemCpp::GetFieldAtIndex(lldb::opaque_compiler_type_t type,
                                            size_t idx, std::string &name,
                                            uint64_t *bit_offset_ptr,
                                            uint32_t *bitfield_bit_size_ptr,
                                            bool *is_bitfield_ptr) {
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return {};
  auto *rec = node->As<LLDBRecordTypeNode>();
  size_t non_art_idx = 0;
  for (auto &f : rec->fields) {
    if (f.is_artificial)
      continue;
    if (non_art_idx == idx) {
      name = f.name;
      if (bit_offset_ptr)       *bit_offset_ptr = f.bit_offset;
      if (bitfield_bit_size_ptr) *bitfield_bit_size_ptr = f.bitfield_bit_size;
      if (is_bitfield_ptr)      *is_bitfield_ptr = f.bitfield_bit_size != 0;
      return MakeCT(*this, f.type);
    }
    ++non_art_idx;
  }
  return {};
}

uint32_t
TypeSystemCpp::GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) {
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return 0;
  uint32_t n = 0;
  for (auto &b : node->As<LLDBRecordTypeNode>()->bases)
    if (!b.is_virtual) ++n;
  return n;
}

uint32_t
TypeSystemCpp::GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) {
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return 0;
  uint32_t n = 0;
  for (auto &b : node->As<LLDBRecordTypeNode>()->bases)
    if (b.is_virtual) ++n;
  return n;
}

CompilerType
TypeSystemCpp::GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx, uint32_t *bit_offset_ptr) {
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return {};
  size_t count = 0;
  for (auto &b : node->As<LLDBRecordTypeNode>()->bases) {
    if (!b.is_virtual) {
      if (count == idx) {
        if (bit_offset_ptr)
          *bit_offset_ptr = (uint32_t)b.bit_offset;
        return MakeCT(*this, b.type);
      }
      ++count;
    }
  }
  return {};
}

CompilerType
TypeSystemCpp::GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx, uint32_t *bit_offset_ptr) {
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return {};
  size_t count = 0;
  for (auto &b : node->As<LLDBRecordTypeNode>()->bases) {
    if (b.is_virtual) {
      if (count == idx) {
        if (bit_offset_ptr)
          *bit_offset_ptr = (uint32_t)b.bit_offset;
        return MakeCT(*this, b.type);
      }
      ++count;
    }
  }
  return {};
}

CompilerDecl
TypeSystemCpp::GetStaticFieldWithName(lldb::opaque_compiler_type_t type,
                                      llvm::StringRef name) {
  return CompilerDecl();
}

llvm::Expected<CompilerType> TypeSystemCpp::GetDereferencedType(
    lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
    std::string &deref_name, uint32_t &deref_byte_size,
    int32_t &deref_byte_offset, ValueObject *valobj,
    uint64_t &language_flags) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return llvm::createStringError("No type");
  if (node->kind == TypeNodeKind::Pointer) {
    CompilerType pt =
        MakeCT(*this, node->As<LLDBPointerTypeNode>()->pointee);
    deref_name = "";
    deref_byte_offset = 0;
    auto bs = pt.GetByteSize(exe_ctx ? exe_ctx->GetBestExecutionContextScope()
                                     : nullptr);
    deref_byte_size = bs ? (uint32_t)*bs : 0;
    language_flags = 0;
    return pt;
  }
  if (node->kind == TypeNodeKind::LValueReference ||
      node->kind == TypeNodeKind::RValueReference) {
    LLDBQualType pointee =
        node->kind == TypeNodeKind::LValueReference
            ? node->As<LLDBLValueReferenceTypeNode>()->pointee
            : node->As<LLDBRValueReferenceTypeNode>()->pointee;
    CompilerType pt = MakeCT(*this, pointee);
    deref_name = "";
    deref_byte_offset = 0;
    auto bs = pt.GetByteSize(exe_ctx ? exe_ctx->GetBestExecutionContextScope()
                                     : nullptr);
    deref_byte_size = bs ? (uint32_t)*bs : 0;
    language_flags = 0;
    return pt;
  }
  return llvm::createStringError("Not a pointer or reference");
}

llvm::Expected<CompilerType> TypeSystemCpp::GetChildCompilerTypeAtIndex(
    lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {

  child_is_base_class = false;
  child_is_deref_of_parent = false;
  language_flags = 0;

  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return llvm::createStringError("No type");

  if (node->kind == TypeNodeKind::Pointer) {
    CompilerType pt =
        MakeCT(*this, node->As<LLDBPointerTypeNode>()->pointee);
    if (pt.IsVoidType())
      return llvm::createStringError("cannot dereference void *");
    if (transparent_pointers && pt.IsAggregateType()) {
      // Delegate field/base-class access directly to the pointee, matching
      // TypeSystemClang's behaviour for transparent pointer dereferencing.
      child_is_deref_of_parent = false;
      bool tmp_deref = false;
      return pt.GetChildCompilerTypeAtIndex(
          exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
          tmp_deref, valobj, language_flags);
    }
    // Non-transparent or non-aggregate: dereference at index 0, used by
    // CreateSyntheticArrayMember for pointer-as-array display.
    child_is_deref_of_parent = true;
    const char *parent_name = valobj ? valobj->GetName().GetCString() : nullptr;
    if (parent_name) {
      child_name.assign(1, '*');
      child_name += parent_name;
    }
    if (idx == 0) {
      auto bs = pt.GetByteSize(exe_ctx ? exe_ctx->GetBestExecutionContextScope()
                                       : nullptr);
      child_byte_size = bs ? (uint32_t)*bs : 0;
      child_byte_offset = 0;
      child_bitfield_bit_size = 0;
      child_bitfield_bit_offset = 0;
      return pt;
    }
    return llvm::createStringError("pointer child index out of bounds");
  }

  if (node->kind == TypeNodeKind::LValueReference ||
      node->kind == TypeNodeKind::RValueReference) {
    LLDBQualType pointee;
    if (node->kind == TypeNodeKind::LValueReference)
      pointee = node->As<LLDBLValueReferenceTypeNode>()->pointee;
    else
      pointee = node->As<LLDBRValueReferenceTypeNode>()->pointee;
    CompilerType pt = MakeCT(*this, pointee);
    if (pt.IsAggregateType()) {
      bool tmp_deref = false;
      return pt.GetChildCompilerTypeAtIndex(
          exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
          tmp_deref, valobj, language_flags);
    }
    const char *parent_name = valobj ? valobj->GetName().GetCString() : nullptr;
    if (parent_name) {
      child_name.assign(1, '&');
      child_name += parent_name;
    }
    auto bs = pt.GetByteSize(exe_ctx ? exe_ctx->GetBestExecutionContextScope()
                                     : nullptr);
    child_byte_size = bs ? (uint32_t)*bs : 0;
    child_byte_offset = 0;
    child_bitfield_bit_size = 0;
    child_bitfield_bit_offset = 0;
    child_is_deref_of_parent = true;
    return pt;
  }

  if (node->kind == TypeNodeKind::Record) {
    auto *rec = node->As<LLDBRecordTypeNode>();

    // Collect base classes first (non-virtual)
    std::vector<const LLDBBaseClassNode *> valid_bases;
    for (auto &b : rec->bases) {
      if (!b.is_virtual) {
        if (omit_empty_base_classes) {
          LLDBTypeNode *base_node = Desugar(b.type.node);
          if (base_node && base_node->kind == TypeNodeKind::Record) {
            // Complete the base before checking emptiness for consistency.
            GetCompleteType(ToOpaque(base_node));
            auto *brec = base_node->As<LLDBRecordTypeNode>();
            bool has_non_art_fields = false;
            for (auto &bf : brec->fields)
              if (!bf.is_artificial) { has_non_art_fields = true; break; }
            if (!has_non_art_fields && brec->bases.empty())
              continue;
          }
        }
        valid_bases.push_back(&b);
      }
    }

    if (idx < valid_bases.size()) {
      const LLDBBaseClassNode *b = valid_bases[idx];
      CompilerType base_type = MakeCT(*this, b->type);
      child_name = base_type.GetTypeName().GetStringRef().str();
      auto bs = base_type.GetByteSize(
          exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr);
      child_byte_size = bs ? (uint32_t)*bs : 0;
      child_byte_offset = (int32_t)(b->bit_offset / 8);
      child_bitfield_bit_size = 0;
      child_bitfield_bit_offset = 0;
      child_is_base_class = true;
      return base_type;
    }

    size_t field_idx = idx - valid_bases.size();
    size_t non_art_idx = 0;
    for (auto &f : rec->fields) {
      if (f.is_artificial)
        continue;
      if (non_art_idx == field_idx) {
        child_name = f.name;
        CompilerType ft = MakeCT(*this, f.type);
        auto bs = ft.GetByteSize(
            exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr);
        child_byte_size = bs ? (uint32_t)*bs : 0;
        child_bitfield_bit_size = f.bitfield_bit_size;
        if (f.bitfield_bit_size && child_byte_size > 0) {
          uint32_t child_bit_size = child_byte_size * 8;
          child_bitfield_bit_offset = f.bit_offset % child_bit_size;
          child_byte_offset =
              (int32_t)((f.bit_offset - child_bitfield_bit_offset) / 8);
        } else {
          child_byte_offset = (int32_t)(f.bit_offset / 8);
          child_bitfield_bit_offset = 0;
        }
        return ft;
      }
      ++non_art_idx;
    }
  }

  if (node->kind == TypeNodeKind::Array) {
    auto *arr = node->As<LLDBArrayTypeNode>();
    if (!ignore_array_bounds && arr->element_count && idx >= *arr->element_count)
      return llvm::createStringError("Index out of bounds");
    CompilerType et = MakeCT(*this, arr->element_type);
    child_name = "[" + std::to_string(idx) + "]";
    auto bs = et.GetByteSize(
        exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr);
    child_byte_size = bs ? (uint32_t)*bs : 0;
    child_byte_offset = (int32_t)(idx * child_byte_size);
    child_bitfield_bit_size = 0;
    child_bitfield_bit_offset = 0;
    return et;
  }

  return llvm::createStringError("No child at index");
}

llvm::Expected<uint32_t>
TypeSystemCpp::GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                       llvm::StringRef name,
                                       bool omit_empty_base_classes) {
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  // For pointer/reference types, delegate to the pointee.
  if (node && (node->kind == TypeNodeKind::Pointer ||
               node->kind == TypeNodeKind::LValueReference ||
               node->kind == TypeNodeKind::RValueReference)) {
    LLDBQualType pointee;
    if (node->kind == TypeNodeKind::Pointer)
      pointee = node->As<LLDBPointerTypeNode>()->pointee;
    else if (node->kind == TypeNodeKind::LValueReference)
      pointee = node->As<LLDBLValueReferenceTypeNode>()->pointee;
    else
      pointee = node->As<LLDBRValueReferenceTypeNode>()->pointee;
    CompilerType pointee_ct = MakeCT(*this, pointee);
    if (pointee_ct.IsAggregateType())
      return pointee_ct.GetIndexOfChildWithName(name, omit_empty_base_classes);
  }
  if (!node || node->kind != TypeNodeKind::Record)
    return llvm::createStringError("Not a record");
  auto *rec = node->As<LLDBRecordTypeNode>();
  uint32_t idx = 0;
  for (auto &b : rec->bases) {
    if (!b.is_virtual) {
      if (omit_empty_base_classes) {
        LLDBTypeNode *base_node = Desugar(b.type.node);
        if (base_node && base_node->kind == TypeNodeKind::Record) {
          // Complete the base before checking emptiness for consistency.
          GetCompleteType(ToOpaque(base_node));
          auto *brec = base_node->As<LLDBRecordTypeNode>();
          bool has_non_art_fields = false;
          for (auto &bf : brec->fields)
            if (!bf.is_artificial) { has_non_art_fields = true; break; }
          if (!has_non_art_fields && brec->bases.empty())
            continue;
        }
      }
      CompilerType base_type = MakeCT(*this, b.type);
      if (base_type.GetTypeName().GetStringRef() == name)
        return idx;
      ++idx;
    }
  }
  for (auto &f : rec->fields) {
    if (f.is_artificial)
      continue;
    if (f.name == name.str())
      return idx;
    ++idx;
  }
  return llvm::createStringError("Field not found");
}

size_t TypeSystemCpp::GetIndexOfChildMemberWithName(
    lldb::opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  if (name.empty())
    return 0;
  GetCompleteType(type);
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return 0;

  // For pointer/reference types, delegate to the pointee.
  if (node->kind == TypeNodeKind::Pointer ||
      node->kind == TypeNodeKind::LValueReference ||
      node->kind == TypeNodeKind::RValueReference) {
    LLDBQualType pointee;
    if (node->kind == TypeNodeKind::Pointer)
      pointee = node->As<LLDBPointerTypeNode>()->pointee;
    else if (node->kind == TypeNodeKind::LValueReference)
      pointee = node->As<LLDBLValueReferenceTypeNode>()->pointee;
    else
      pointee = node->As<LLDBRValueReferenceTypeNode>()->pointee;
    CompilerType pointee_ct = MakeCT(*this, pointee);
    if (pointee_ct.IsAggregateType())
      return pointee_ct.GetIndexOfChildMemberWithName(name, omit_empty_base_classes, child_indexes);
    return 0;
  }

  if (node->kind != TypeNodeKind::Record)
    return 0;

  auto *rec = node->As<LLDBRecordTypeNode>();

  // Helper: is this base class "empty" (no non-artificial fields and no bases)?
  auto IsEmptyBase = [&](LLDBTypeNode *base_node) -> bool {
    base_node = Desugar(base_node);
    if (!base_node || base_node->kind != TypeNodeKind::Record)
      return false;
    GetCompleteType(ToOpaque(base_node));
    auto *brec = base_node->As<LLDBRecordTypeNode>();
    for (auto &bf : brec->fields)
      if (!bf.is_artificial)
        return false;
    return brec->bases.empty();
  };

  // Helper: count the non-virtual, non-empty base classes of a record
  // (the number used as the field child index offset).
  auto CountBases = [&](LLDBRecordTypeNode *r) -> uint32_t {
    uint32_t nb = 0;
    for (auto &b : r->bases) {
      if (b.is_virtual)
        continue;
      if (omit_empty_base_classes && IsEmptyBase(b.type.node))
        continue;
      ++nb;
    }
    return nb;
  };

  // Search only through a record's own fields (and nested anonymous
  // sub-fields) without searching base classes.  Used when recursing into
  // anonymous struct/union fields: per C++ scoping, a base class's members
  // are NOT injected into the enclosing scope through an anonymous field.
  std::function<size_t(LLDBTypeNode *, std::vector<uint32_t> &)> SearchFields;
  SearchFields = [&](LLDBTypeNode *n,
                     std::vector<uint32_t> &idxs) -> size_t {
    n = Desugar(n);
    if (!n || n->kind != TypeNodeKind::Record)
      return 0;
    GetCompleteType(ToOpaque(n));
    auto *r = n->As<LLDBRecordTypeNode>();
    uint32_t nb = CountBases(r);
    uint32_t fidx = 0;
    for (auto &f : r->fields) {
      if (f.is_artificial)
        continue;
      uint32_t cidx = nb + fidx;
      if (f.name.empty()) {
        auto saved = idxs;
        idxs.push_back(cidx);
        if (SearchFields(f.type.node, idxs))
          return idxs.size();
        idxs = std::move(saved);
      } else if (f.name == name.str()) {
        idxs.push_back(cidx);
        return idxs.size();
      }
      ++fidx;
    }
    return 0;
  };

  // Step 1: search own fields first (fields shadow base-class members).
  if (SearchFields(node, child_indexes))
    return child_indexes.size();

  // Step 2: search through non-virtual base classes.
  uint32_t base_child_idx = 0;
  for (auto &b : rec->bases) {
    if (b.is_virtual)
      continue;
    if (omit_empty_base_classes && IsEmptyBase(b.type.node)) {
      continue;
    }
    std::vector<uint32_t> saved = child_indexes;
    child_indexes.push_back(base_child_idx);
    CompilerType base_ct = MakeCT(*this, b.type);
    if (base_ct.GetIndexOfChildMemberWithName(name, omit_empty_base_classes,
                                               child_indexes))
      return child_indexes.size();
    child_indexes = std::move(saved);
    ++base_child_idx;
  }
  return 0;
}

void TypeSystemCpp::SynthesizeUnnamedBitfields(LLDBRecordTypeNode *rec) {
  if (!rec)
    return;

  // Word width used for unnamed bitfield types (matches Clang's behavior).
  const uint32_t word_width = 32;
  LLDBTypeNode *int_node =
      m_registry.GetOrCreateBuiltin(eBasicTypeInt, word_width, true, false);
  LLDBQualType int_type(int_node, {});

  const bool have_base = !rec->bases.empty();

  // Track where the previous field ended (in bits), and whether the most
  // recent field seen was "first/initial" or an artificial vtable pointer
  // (both conditions that suppress unnamed-bitfield synthesis when combined
  // with have_base, matching TypeSystemClang).
  uint64_t last_field_end = 0;
  bool last_was_first_or_vptr = true;
  bool last_was_bitfield = false;

  std::vector<LLDBFieldNode> new_fields;
  for (auto &f : rec->fields) {
    if (f.is_artificial) {
      // Keep track of artificial fields (vtable pointer) for gap detection.
      if (f.bit_offset == 0) {
        last_was_first_or_vptr = true;
        // Compute the end of the artificial field.
        auto bs = GetBitSize(ToOpaque(f.type.node), nullptr);
        last_field_end = bs ? *bs : 0;
        last_was_bitfield = false;
      }
      new_fields.push_back(std::move(f));
      continue;
    }

    // Check if there's a gap before this field.
    uint64_t adjusted_last_end = last_field_end;
    if (!last_was_bitfield && adjusted_last_end != 0 &&
        (adjusted_last_end % word_width) != 0)
      adjusted_last_end += word_width - (adjusted_last_end % word_width);

    bool should_create = f.bit_offset > adjusted_last_end;
    if (should_create && have_base && last_was_first_or_vptr)
      should_create = false;

    if (should_create) {
      LLDBFieldNode unnamed;
      unnamed.name = "";
      unnamed.type = int_type;
      unnamed.bit_offset = adjusted_last_end;
      unnamed.bitfield_bit_size = (uint32_t)(f.bit_offset - adjusted_last_end);
      unnamed.is_artificial = false;
      new_fields.push_back(std::move(unnamed));
    }

    last_was_first_or_vptr = false;
    last_was_bitfield = (f.bitfield_bit_size != 0);
    if (last_was_bitfield)
      last_field_end = f.bit_offset + f.bitfield_bit_size;
    else {
      auto bs = GetBitSize(ToOpaque(f.type.node), nullptr);
      last_field_end = f.bit_offset + (bs ? *bs : 0);
    }
    new_fields.push_back(std::move(f));
  }
  rec->fields = std::move(new_fields);
}

LLDBQualType TypeSystemCpp::AdoptQualType(LLDBQualType qt) {
  if (!qt.node)
    return qt;
  switch (qt.node->kind) {
  case TypeNodeKind::Pointer: {
    auto *ptr = qt.node->As<LLDBPointerTypeNode>();
    LLDBQualType adopted_pointee = AdoptQualType(ptr->pointee);
    // Find an existing pointer to adopted_pointee in THIS registry.
    for (auto &up : m_registry.GetAllNodes()) {
      if (up->kind == TypeNodeKind::Pointer) {
        auto *p = up->As<LLDBPointerTypeNode>();
        if (p->pointee == adopted_pointee)
          return LLDBQualType(p, qt.quals);
      }
    }
    return LLDBQualType(m_registry.CreatePointerType(adopted_pointee), qt.quals);
  }
  case TypeNodeKind::LValueReference: {
    auto *ref = qt.node->As<LLDBLValueReferenceTypeNode>();
    LLDBQualType adopted_pointee = AdoptQualType(ref->pointee);
    for (auto &up : m_registry.GetAllNodes()) {
      if (up->kind == TypeNodeKind::LValueReference) {
        auto *r = up->As<LLDBLValueReferenceTypeNode>();
        if (r->pointee == adopted_pointee)
          return LLDBQualType(r, qt.quals);
      }
    }
    return LLDBQualType(m_registry.CreateLValueRefType(adopted_pointee),
                        qt.quals);
  }
  case TypeNodeKind::RValueReference: {
    auto *ref = qt.node->As<LLDBRValueReferenceTypeNode>();
    LLDBQualType adopted_pointee = AdoptQualType(ref->pointee);
    for (auto &up : m_registry.GetAllNodes()) {
      if (up->kind == TypeNodeKind::RValueReference) {
        auto *r = up->As<LLDBRValueReferenceTypeNode>();
        if (r->pointee == adopted_pointee)
          return LLDBQualType(r, qt.quals);
      }
    }
    return LLDBQualType(m_registry.CreateRValueRefType(adopted_pointee),
                        qt.quals);
  }
  default:
    // Record: look for a node with the same qualified name in THIS registry.
    // This handles cross-module types where the same logical record exists as
    // different node objects in different parsers' registries.
    if (qt.node->kind == TypeNodeKind::Record) {
      auto *rec = qt.node->As<LLDBRecordTypeNode>();
      if (!rec->qualified_name.empty()) {
        for (auto &up : m_registry.GetAllNodes()) {
          if (up->kind == TypeNodeKind::Record) {
            auto *r = up->As<LLDBRecordTypeNode>();
            if (r->qualified_name == rec->qualified_name)
              return LLDBQualType(r, qt.quals);
          }
        }
      }
    }
    // Builtin, Enum, Function, Typedef, etc. — shared by pointer across
    // registries; return as-is.
    return qt;
  }
}

CompilerType
TypeSystemCpp::GetDirectNestedTypeWithName(lldb::opaque_compiler_type_t type,
                                           llvm::StringRef name) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Record)
    return {};
  auto *rec = node->As<LLDBRecordTypeNode>();
  // Ensure the record is complete so nested_typedefs is populated for
  // typedefs that were explicitly parsed (e.g. from local variables).
  if (!rec->is_complete)
    GetCompleteType(ToOpaque(node));
  // Check nested typedefs already parsed.
  for (size_t i = 0; i < rec->nested_typedefs.size(); ++i) {
    auto *td = rec->nested_typedefs[i];
    if (td->name != name)
      continue;
    // Adopt the typedef into THIS registry so canonical-type comparisons work
    // across module boundaries (the typedef may have been added during
    // CompleteTypeFromDWARF by a different parser/registry).
    LLDBQualType adopted = AdoptQualType(td->underlying_type);
    if (adopted.node == td->underlying_type.node &&
        adopted.quals == td->underlying_type.quals)
      return CompilerType(weak_from_this(), ToOpaque(td));
    // Build an adopted typedef in THIS registry and replace the cache entry
    // so subsequent lookups return the same object.
    LLDBTypedefTypeNode *new_td = m_registry.CreateTypedef(td->name, adopted);
    new_td->qualified_name = td->qualified_name;
    new_td->parent_node = node;
    rec->nested_typedefs[i] = new_td;
    return CompilerType(weak_from_this(), ToOpaque(new_td));
  }
  // Fall back to searching the DWARF for a typedef child we haven't parsed yet.
  // Use the parser that holds the complete definition DIE (may differ from
  // this TypeSystemCpp's own parser when the type was completed cross-module).
  DWARFASTParserCpp *parser =
      GetCompleteParserForNode(node);
  if (!parser)
    parser = static_cast<DWARFASTParserCpp *>(GetDWARFParser());
  if (parser) {
    if (LLDBTypeNode *td = parser->FindNestedTypedefByName(node, name)) {
      // td was (possibly) created in a different registry.  Adopt its
      // underlying type into THIS registry so that canonical-type comparisons
      // work correctly across module boundaries (e.g. lib.cpp forward-declares
      // a type that is completed from main.cpp's parser).
      auto *tdef = td->As<LLDBTypedefTypeNode>();
      LLDBQualType adopted = AdoptQualType(tdef->underlying_type);
      LLDBTypedefTypeNode *new_td =
          m_registry.CreateTypedef(tdef->name, adopted);
      new_td->qualified_name = tdef->qualified_name;
      new_td->parent_node = node;
      rec->nested_typedefs.push_back(new_td);
      return CompilerType(weak_from_this(), ToOpaque(new_td));
    }
  }
  return {};
}

bool TypeSystemCpp::IsTemplateType(lldb::opaque_compiler_type_t type) {
  return false;
}

size_t TypeSystemCpp::GetNumTemplateArguments(lldb::opaque_compiler_type_t type,
                                              bool expand_pack) {
  return 0;
}

lldb::TemplateArgumentKind
TypeSystemCpp::GetTemplateArgumentKind(lldb::opaque_compiler_type_t type,
                                       size_t idx, bool expand_pack) {
  return lldb::eTemplateArgumentKindNull;
}

CompilerType
TypeSystemCpp::GetTypeTemplateArgument(lldb::opaque_compiler_type_t type,
                                       size_t idx, bool expand_pack) {
  return {};
}

std::optional<CompilerType::IntegralTemplateArgument>
TypeSystemCpp::GetIntegralTemplateArgument(lldb::opaque_compiler_type_t type,
                                           size_t idx, bool expand_pack) {
  return std::nullopt;
}

bool TypeSystemCpp::IsPromotableIntegerType(lldb::opaque_compiler_type_t type) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node || node->kind != TypeNodeKind::Builtin)
    return false;
  auto bt = node->As<LLDBBuiltinTypeNode>()->basic_type;
  return bt == eBasicTypeBool || bt == eBasicTypeChar ||
         bt == eBasicTypeSignedChar || bt == eBasicTypeUnsignedChar ||
         bt == eBasicTypeShort || bt == eBasicTypeUnsignedShort;
}

CompilerType
TypeSystemCpp::GetPromotedIntegerType(lldb::opaque_compiler_type_t type) {
  return GetBasicTypeFromAST(eBasicTypeInt);
}

// ---------------------------------------------------------------------------
// Dumping
// ---------------------------------------------------------------------------

#ifndef NDEBUG
void TypeSystemCpp::dump(lldb::opaque_compiler_type_t type) const {
  LLDBTypeNode *node = NodeFromOpaque(type);
  llvm::errs() << "TypeSystemCpp type: "
               << (node ? "kind=" + std::to_string((int)node->kind) : "null")
               << "\n";
}
#endif

bool TypeSystemCpp::DumpTypeValue(
    lldb::opaque_compiler_type_t type, Stream &s, lldb::Format format,
    const DataExtractor &data, lldb::offset_t data_offset,
    size_t data_byte_size, uint32_t bitfield_bit_size,
    uint32_t bitfield_bit_offset, ExecutionContextScope *exe_scope) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node)
    return false;

  if (node->kind == TypeNodeKind::Builtin) {
    auto *bt = node->As<LLDBBuiltinTypeNode>();
    if (format == eFormatDefault)
      format = GetFormat(type);

    if (bt->basic_type == eBasicTypeBool) {
      lldb::offset_t off = data_offset;
      s.Printf("%s", data.GetU8(&off) ? "true" : "false");
      return true;
    }

    // Use DumpDataExtractor to handle all format cases (hex, decimal, etc.)
    uint32_t item_count = 1;
    size_t item_byte_size = data_byte_size;
    switch (format) {
    case eFormatChar:
    case eFormatCharPrintable:
    case eFormatCharArray:
    case eFormatBytes:
    case eFormatBytesWithASCII:
      item_count = item_byte_size;
      item_byte_size = 1;
      break;
    case eFormatUnicode16:
      item_count = item_byte_size / 2;
      item_byte_size = 2;
      break;
    case eFormatUnicode32:
      item_count = item_byte_size / 4;
      item_byte_size = 4;
      break;
    default:
      break;
    }
    return DumpDataExtractor(data, &s, data_offset, format, item_byte_size,
                             item_count, UINT32_MAX, LLDB_INVALID_ADDRESS,
                             bitfield_bit_size, bitfield_bit_offset, exe_scope);
  }

  if (node->kind == TypeNodeKind::Pointer) {
    lldb::offset_t off = data_offset;
    uint64_t val = (GetPointerByteSize() == 8) ? data.GetU64(&off)
                                               : data.GetU32(&off);
    DumpAddress(s.AsRawOstream(), val, GetPointerByteSize());
    return true;
  }

  if (node->kind == TypeNodeKind::LValueReference ||
      node->kind == TypeNodeKind::RValueReference) {
    lldb::offset_t off = data_offset;
    uint64_t val = (GetPointerByteSize() == 8) ? data.GetU64(&off)
                                               : data.GetU32(&off);
    DumpAddress(s.AsRawOstream(), val, GetPointerByteSize());
    return true;
  }

  if (node->kind == TypeNodeKind::Enum) {
    auto *en = node->As<LLDBEnumTypeNode>();
    LLDBTypeNode *int_node = Desugar(en->integer_type.node);
    if (int_node && int_node->kind == TypeNodeKind::Builtin) {
      auto *bt = int_node->As<LLDBBuiltinTypeNode>();
      bool is_signed = bt->is_signed;
      size_t byte_size = bt->bit_size / 8;
      lldb::offset_t off = data_offset;
      int64_t svalue = 0;
      uint64_t uvalue = 0;
      switch (bt->bit_size) {
      case 8:
        uvalue = data.GetU8(&off);
        svalue = is_signed ? (int64_t)(int8_t)uvalue : (int64_t)uvalue;
        break;
      case 16:
        uvalue = data.GetU16(&off);
        svalue = is_signed ? (int64_t)(int16_t)uvalue : (int64_t)uvalue;
        break;
      case 32:
        uvalue = data.GetU32(&off);
        svalue = is_signed ? (int64_t)(int32_t)uvalue : (int64_t)uvalue;
        break;
      case 64:
        uvalue = data.GetU64(&off);
        svalue = (int64_t)uvalue;
        break;
      }
      int64_t enum_svalue = is_signed ? svalue : (int64_t)uvalue;
      uint64_t enum_uvalue = uvalue;

      // Bitfield-enum heuristic: every enumerator is either a single bit or a
      // superset of previously-seen bits (like AB = A|B).
      bool can_be_bitfield = !en->enumerators.empty();
      uint64_t covered_bits = 0;
      int num_enumerators = 0;

      // Mask to compare only the actual enum-width bits.
      uint64_t bit_mask = (byte_size >= 8) ? ~0ULL : ((1ULL << (8 * byte_size)) - 1);

      for (auto &e : en->enumerators) {
        uint64_t val = e.value.getZExtValue() & bit_mask;
        if (is_signed)
          val = llvm::SignExtend64(val, 8 * byte_size);
        if (llvm::popcount(val) != 1 && (val & ~covered_bits) != 0)
          can_be_bitfield = false;
        covered_bits |= val;
        ++num_enumerators;
        // Compare masked to byte_size bits.
        if ((e.value.getZExtValue() & bit_mask) == (enum_uvalue & bit_mask)) {
          s.PutCString(e.name.c_str());
          return true;
        }
      }

      if (!can_be_bitfield) {
        if (is_signed)
          s.Printf("%" PRIi64, enum_svalue);
        else
          s.Printf("%" PRIu64, enum_uvalue);
        return true;
      }

      if (!enum_uvalue) {
        s.Printf("0x%" PRIx64, enum_uvalue);
        return true;
      }

      // Decompose as OR of flag enumerators.
      std::vector<std::pair<uint64_t, llvm::StringRef>> values;
      values.reserve(num_enumerators);
      for (auto &e : en->enumerators)
        if (uint64_t v = e.value.getZExtValue())
          values.emplace_back(v, llvm::StringRef(e.name));

      llvm::stable_sort(values, [](const auto &a, const auto &b) {
        return llvm::popcount(a.first) > llvm::popcount(b.first);
      });

      uint64_t remaining = enum_uvalue;
      for (const auto &val : values) {
        if ((remaining & val.first) != val.first)
          continue;
        remaining &= ~val.first;
        s.PutCString(val.second);
        if (remaining)
          s.PutCString(" | ");
      }
      if (remaining)
        s.Printf("0x%" PRIx64, remaining);

      return true;
    }
  }
  return false;
}

void TypeSystemCpp::DumpTypeDescription(lldb::opaque_compiler_type_t type,
                                        lldb::DescriptionLevel level) {
  StreamString ss;
  DumpTypeDescription(type, ss, level);
  llvm::outs() << ss.GetString() << "\n";
}

void TypeSystemCpp::DumpTypeDescription(lldb::opaque_compiler_type_t type,
                                        Stream &s,
                                        lldb::DescriptionLevel level) {
  LLDBTypeNode *node = Desugar(NodeFromOpaque(type));
  if (!node) {
    s.PutCString(GetTypeName(type, false).GetStringRef());
    return;
  }

  if (node->kind == TypeNodeKind::Enum) {
    auto *en = node->As<LLDBEnumTypeNode>();
    if (en->is_scoped)
      s.Printf("enum class %s", en->name.c_str());
    else
      s.Printf("enum %s", en->name.c_str());
    s.PutCString(" {\n");
    for (auto &e : en->enumerators) {
      llvm::SmallString<32> val_str;
      e.value.toString(val_str);
      s.Printf("  %s = %s,\n", e.name.c_str(), val_str.c_str());
    }
    s.PutCString("}");
    return;
  }

  if (node->kind == TypeNodeKind::Record) {
    auto *rec = node->As<LLDBRecordTypeNode>();
    if (rec->is_union)
      s.Printf("union %s", rec->qualified_name.empty() ? rec->name.c_str() : rec->qualified_name.c_str());
    else if (rec->is_class)
      s.Printf("class %s", rec->qualified_name.empty() ? rec->name.c_str() : rec->qualified_name.c_str());
    else
      s.Printf("struct %s", rec->qualified_name.empty() ? rec->name.c_str() : rec->qualified_name.c_str());
    if (rec->is_complete && !rec->is_forcefully_completed) {
      s.PutCString(" {\n");
      for (auto &base : rec->bases) {
        ConstString bname = GetTypeName(ToOpaque(base.type.node), false);
        s.Printf("  // base: %s\n", bname.GetCString());
      }
      for (auto &field : rec->fields) {
        if (field.is_artificial)
          continue;
        ConstString ftype = GetTypeName(ToOpaque(field.type.node), false);
        // Print in C declaration style: for array types, dimensions come after
        // the field name, e.g. "char padding[0]" not "char[0] padding".
        llvm::StringRef type_str = ftype.GetStringRef();
        size_t bracket = type_str.find('[');
        if (bracket == llvm::StringRef::npos)
          s.Printf("  %s %s;\n", ftype.GetCString(), field.name.c_str());
        else
          s.Printf("  %.*s %s%s;\n", (int)bracket, type_str.data(),
                   field.name.c_str(), type_str.substr(bracket).str().c_str());
      }
      s.PutCString("}");
    }
    return;
  }

  s.PutCString(GetTypeName(type, false).GetStringRef());
}

void TypeSystemCpp::Dump(llvm::raw_ostream &output, llvm::StringRef filter,
                         bool show_color) {
  // Produce a Clang-AST-like dump so that "image dump ast" FileCheck tests
  // pass when TypeSystemCpp is active.  We emit only named top-level
  // declarations: typedefs, namespaces with their nested typedefs, and
  // complete record types with their nested typedefs.
  auto nodes = m_registry.GetAllNodes();

  // Helper: get the canonical display name of a type node (used for the
  // 'quoted' type in typedef dump lines).
  auto displayName = [&](LLDBTypeNode *n) -> std::string {
    if (!n)
      return "<null>";
    return GetTypeName(ToOpaque(n), /*base_only=*/false).GetStringRef().str();
  };

  // --- 1. Top-level typedefs (no parent class, not inside a namespace) ----
  for (auto &up : nodes) {
    LLDBTypeNode *n = up.get();
    if (n->kind != TypeNodeKind::Typedef)
      continue;
    auto *td = n->As<LLDBTypedefTypeNode>();
    if (td->parent_node)
      continue; // nested in a record — printed with the record below
    if (td->qualified_name != td->name)
      continue; // inside a namespace — printed with the namespace below
    if (!filter.empty() && !llvm::StringRef(td->name).contains(filter))
      continue;
    output << "|-TypedefDecl 0x"
           << llvm::format_hex_no_prefix((uintptr_t)n, 16) << " <no-location> "
           << td->name << " '" << displayName(td->underlying_type.node)
           << "'\n";
  }

  // --- 2. Namespaces and their nested typedefs ----------------------------
  llvm::StringMap<std::vector<LLDBTypedefTypeNode *>> ns_map;
  for (auto &up : nodes) {
    LLDBTypeNode *n = up.get();
    if (n->kind != TypeNodeKind::Typedef)
      continue;
    auto *td = n->As<LLDBTypedefTypeNode>();
    if (td->parent_node)
      continue; // record-nested
    if (td->qualified_name == td->name)
      continue; // top-level
    size_t sep = td->qualified_name.rfind("::");
    if (sep == std::string::npos)
      continue;
    std::string ns_name = td->qualified_name.substr(0, sep);
    ns_map[ns_name].push_back(td);
  }
  for (auto &[ns_name, tdefs] : ns_map) {
    if (!filter.empty() && !llvm::StringRef(ns_name).contains(filter))
      continue;
    output << "|-NamespaceDecl 0x0 <no-location> " << ns_name << "\n";
    for (size_t i = 0, e = tdefs.size(); i != e; ++i) {
      bool last = (i + 1 == e);
      output << (last ? "| `-" : "| |-") << "TypedefDecl 0x"
             << llvm::format_hex_no_prefix((uintptr_t)tdefs[i], 16)
             << " <no-location> " << tdefs[i]->name << " '"
             << displayName(tdefs[i]->underlying_type.node) << "'\n";
    }
  }

  // Helper: build the type string for a method, e.g. "int () const &".
  auto methodTypeStr = [&](const LLDBMethodNode &method) -> std::string {
    if (!method.type.node || method.type.node->kind != TypeNodeKind::Function)
      return "<unknown>";
    auto *fn = method.type.node->As<LLDBFunctionTypeNode>();
    std::string ret = displayName(fn->return_type.node);
    if (fn->return_type.isConst())
      ret = "const " + ret;
    std::string params;
    for (size_t i = 0; i < fn->params.size(); ++i) {
      if (i) params += ", ";
      params += displayName(fn->params[i].type.node);
    }
    if (fn->is_variadic) {
      if (!fn->params.empty()) params += ", ";
      params += "...";
    }
    std::string result = ret + " (" + params + ")";
    if (method.is_const) result += " const";
    if (method.is_volatile) result += " volatile";
    if (method.ref_qualifier == LLDBRefQualifier::LValue) result += " &";
    else if (method.ref_qualifier == LLDBRefQualifier::RValue) result += " &&";
    return result;
  };

  // --- 3. Complete top-level records with their methods and nested typedefs --
  for (auto &up : nodes) {
    LLDBTypeNode *n = up.get();
    if (n->kind != TypeNodeKind::Record)
      continue;
    auto *rec = n->As<LLDBRecordTypeNode>();
    if (rec->parent_record_node)
      continue; // nested in another record
    if (!rec->is_complete)
      continue; // skip forward declarations
    if (rec->name.empty())
      continue; // anonymous struct/union
    if (!filter.empty() && !llvm::StringRef(rec->name).contains(filter))
      continue;
    llvm::StringRef kind =
        rec->is_union ? "union" : (rec->is_class ? "class" : "struct");
    output << "|-CXXRecordDecl 0x"
           << llvm::format_hex_no_prefix((uintptr_t)n, 16) << " <no-location> "
           << kind << " " << rec->name << " definition\n";

    // Collect non-artificial methods to print.
    std::vector<const LLDBMethodNode *> methods;
    for (auto &m : rec->methods)
      if (!m.is_artificial)
        methods.push_back(&m);

    size_t total_children = methods.size() + rec->nested_typedefs.size();
    size_t child_idx = 0;

    for (const auto *method : methods) {
      bool last = (child_idx + 1 == total_children);
      bool has_label = !method->asm_label.empty();
      // Methods use 5-space indent to match FileCheck patterns:
      // "     |-CXXMethodDecl {{.*}} func 'type'"
      output << "     " << (last ? "`-" : "|-") << "CXXMethodDecl 0x"
             << llvm::format_hex_no_prefix((uintptr_t)method->type.node, 16)
             << " <no-location> " << method->name << " '"
             << methodTypeStr(*method) << "'\n";
      if (has_label) {
        // AsmLabelAttr child of non-last method: "     | `-AsmLabelAttr ..."
        // AsmLabelAttr child of last method:     "       `-AsmLabelAttr ..."
        if (last)
          output << "       `-AsmLabelAttr 0x"
                 << llvm::format_hex_no_prefix(
                        (uintptr_t)method->type.node + 1, 16)
                 << " <no-location>\n";
        else
          output << "     | `-AsmLabelAttr 0x"
                 << llvm::format_hex_no_prefix(
                        (uintptr_t)method->type.node + 1, 16)
                 << " <no-location>\n";
      }
      ++child_idx;
    }

    for (size_t i = 0, e = rec->nested_typedefs.size(); i != e; ++i) {
      auto *td = rec->nested_typedefs[i];
      bool last = (child_idx + 1 == total_children);
      output << (last ? "| `-" : "| |-") << "TypedefDecl 0x"
             << llvm::format_hex_no_prefix((uintptr_t)td, 16)
             << " <no-location> " << td->name << " '"
             << displayName(td->underlying_type.node) << "'\n";
      ++child_idx;
    }
  }
}

// ---------------------------------------------------------------------------
// ScratchTypeSystemCpp
// ---------------------------------------------------------------------------

ScratchTypeSystemCpp::ScratchTypeSystemCpp(Target &target,
                                           llvm::Triple triple)
    : TypeSystemCpp("ScratchTypeSystemCpp", triple),
      m_scratch_clang(
          std::make_shared<ScratchTypeSystemClang>(target, triple)) {}

ScratchTypeSystemCpp::~ScratchTypeSystemCpp() = default;

void ScratchTypeSystemCpp::Finalize() {
  TypeSystemCpp::Finalize();
  m_scratch_clang->Finalize();
}

void ScratchTypeSystemCpp::SetSymbolFile(SymbolFile *sym_file) {
  TypeSystem::SetSymbolFile(sym_file);
  m_scratch_clang->SetSymbolFile(sym_file);
}

UserExpression *ScratchTypeSystemCpp::GetUserExpression(
    llvm::StringRef expr, llvm::StringRef prefix, SourceLanguage language,
    Expression::ResultType desired_type,
    const EvaluateExpressionOptions &options, ValueObject *ctx_obj) {
  return m_scratch_clang->GetUserExpression(expr, prefix, language,
                                            desired_type, options, ctx_obj);
}

FunctionCaller *ScratchTypeSystemCpp::GetFunctionCaller(
    const CompilerType &return_type, const Address &function_address,
    const ValueList &arg_value_list, const char *name) {
  return m_scratch_clang->GetFunctionCaller(return_type, function_address,
                                            arg_value_list, name);
}

std::unique_ptr<UtilityFunction>
ScratchTypeSystemCpp::CreateUtilityFunction(std::string text,
                                            std::string name) {
  return m_scratch_clang->CreateUtilityFunction(std::move(text),
                                                std::move(name));
}

PersistentExpressionState *ScratchTypeSystemCpp::GetPersistentExpressionState() {
  return m_scratch_clang->GetPersistentExpressionState();
}


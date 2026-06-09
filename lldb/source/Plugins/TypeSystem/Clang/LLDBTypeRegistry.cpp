//===-- LLDBTypeRegistry.cpp - LLDBTypeRegistry implementation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LLDBTypeIR.h"

#include "llvm/ADT/DenseMap.h"

using namespace lldb_private;

// ============================================================================
// LLDBTypeRegistry – builtin cache
// ============================================================================

LLDBBuiltinTypeNode *
LLDBTypeRegistry::GetOrCreateBuiltin(lldb::BasicType basic_type,
                                     uint32_t bit_size, bool is_signed,
                                     bool is_float) {
  unsigned idx = static_cast<unsigned>(basic_type);
  if (idx < m_builtin_cache.size() && m_builtin_cache[idx])
    return m_builtin_cache[idx];

  auto *node = Alloc<LLDBBuiltinTypeNode>(basic_type, bit_size, is_signed,
                                          is_float);
  if (idx >= m_builtin_cache.size())
    m_builtin_cache.resize(idx + 1, nullptr);
  m_builtin_cache[idx] = node;
  return node;
}

LLDBBuiltinTypeNode *LLDBTypeRegistry::CreateBitIntType(uint32_t bit_size,
                                                        bool is_signed) {
  return Alloc<LLDBBuiltinTypeNode>(lldb::eBasicTypeOther, bit_size, is_signed,
                                    /*is_float=*/false);
}

// ============================================================================
// Derived types
// ============================================================================

LLDBPointerTypeNode *LLDBTypeRegistry::CreatePointerType(LLDBQualType pointee) {
  return Alloc<LLDBPointerTypeNode>(pointee);
}

LLDBLValueReferenceTypeNode *
LLDBTypeRegistry::CreateLValueRefType(LLDBQualType pointee) {
  return Alloc<LLDBLValueReferenceTypeNode>(pointee);
}

LLDBRValueReferenceTypeNode *
LLDBTypeRegistry::CreateRValueRefType(LLDBQualType pointee) {
  return Alloc<LLDBRValueReferenceTypeNode>(pointee);
}

LLDBBlockPointerTypeNode *
LLDBTypeRegistry::CreateBlockPointerType(LLDBQualType fn_type) {
  return Alloc<LLDBBlockPointerTypeNode>(fn_type);
}

LLDBArrayTypeNode *LLDBTypeRegistry::CreateArrayType(LLDBQualType elem,
                                                     std::optional<uint64_t> count,
                                                     bool is_vector) {
  return Alloc<LLDBArrayTypeNode>(elem, count, is_vector);
}

LLDBAtomicTypeNode *LLDBTypeRegistry::CreateAtomicType(LLDBQualType value_type) {
  return Alloc<LLDBAtomicTypeNode>(value_type);
}

LLDBComplexTypeNode *LLDBTypeRegistry::CreateComplexType(LLDBTypeNode *element_type) {
  return Alloc<LLDBComplexTypeNode>(element_type);
}

LLDBMemberPointerTypeNode *
LLDBTypeRegistry::CreateMemberPointerType(LLDBQualType cls,
                                          LLDBQualType pointee) {
  return Alloc<LLDBMemberPointerTypeNode>(cls, pointee);
}

// ============================================================================
// Structural types
// ============================================================================

LLDBRecordTypeNode *LLDBTypeRegistry::CreateRecordType(llvm::StringRef name,
                                                       bool is_union,
                                                       bool is_class) {
  return Alloc<LLDBRecordTypeNode>(name, is_union, is_class);
}

LLDBEnumTypeNode *LLDBTypeRegistry::CreateEnumType(llvm::StringRef name,
                                                   LLDBQualType int_type,
                                                   bool is_scoped) {
  return Alloc<LLDBEnumTypeNode>(name, int_type, is_scoped);
}

LLDBFunctionTypeNode *
LLDBTypeRegistry::CreateFunctionType(LLDBQualType ret,
                                     std::vector<LLDBParamNode> params,
                                     bool is_variadic) {
  return Alloc<LLDBFunctionTypeNode>(ret, std::move(params), is_variadic);
}

LLDBTypedefTypeNode *LLDBTypeRegistry::CreateTypedef(llvm::StringRef name,
                                                     LLDBQualType underlying) {
  return Alloc<LLDBTypedefTypeNode>(name, underlying);
}

LLDBObjCInterfaceTypeNode *
LLDBTypeRegistry::CreateObjCInterfaceType(llvm::StringRef name) {
  return Alloc<LLDBObjCInterfaceTypeNode>(name);
}

LLDBObjCObjectPointerTypeNode *
LLDBTypeRegistry::CreateObjCObjectPointerType(LLDBQualType iface) {
  return Alloc<LLDBObjCObjectPointerTypeNode>(iface);
}

// ============================================================================
// Namespace nodes
// ============================================================================

LLDBNamespaceNode *
LLDBTypeRegistry::GetOrCreateNamespace(llvm::StringRef qualified_name,
                                       LLDBNamespaceNode *parent,
                                       bool is_inline) {
  auto it = m_namespace_nodes.find(qualified_name);
  if (it != m_namespace_nodes.end())
    return it->second.get();

  // Extract the short name from the qualified name
  llvm::StringRef short_name = qualified_name;
  auto sep = qualified_name.rfind("::");
  if (sep != llvm::StringRef::npos)
    short_name = qualified_name.substr(sep + 2);

  auto node = std::make_unique<LLDBNamespaceNode>(short_name, qualified_name,
                                                  parent, is_inline);
  LLDBNamespaceNode *ptr = node.get();
  m_namespace_nodes.insert({qualified_name, std::move(node)});
  return ptr;
}

LLDBNamespaceNode *
LLDBTypeRegistry::FindNamespace(llvm::StringRef qualified_name) const {
  auto it = m_namespace_nodes.find(qualified_name);
  if (it != m_namespace_nodes.end())
    return it->second.get();
  return nullptr;
}

// ============================================================================
// CloneNode – deep copy of a node into this registry
//
// This replaces ASTImporter: instead of copying clang::Decl trees between
// clang::ASTContext instances, we clone IR nodes between registries. The
// cloned node is owned by *this* registry.
// ============================================================================

LLDBTypeNode *LLDBTypeRegistry::CloneNode(const LLDBTypeNode *src) {
  if (!src)
    return nullptr;

  switch (src->kind) {
  case TypeNodeKind::Builtin: {
    const auto *b = src->As<const LLDBBuiltinTypeNode>();
    return GetOrCreateBuiltin(b->basic_type, b->bit_size, b->is_signed,
                              b->is_float);
  }

  case TypeNodeKind::Pointer: {
    const auto *p = src->As<const LLDBPointerTypeNode>();
    LLDBQualType cloned_pointee(CloneNode(p->pointee.node), p->pointee.quals);
    return CreatePointerType(cloned_pointee);
  }

  case TypeNodeKind::LValueReference: {
    const auto *r = src->As<const LLDBLValueReferenceTypeNode>();
    LLDBQualType cloned(CloneNode(r->pointee.node), r->pointee.quals);
    return CreateLValueRefType(cloned);
  }

  case TypeNodeKind::RValueReference: {
    const auto *r = src->As<const LLDBRValueReferenceTypeNode>();
    LLDBQualType cloned(CloneNode(r->pointee.node), r->pointee.quals);
    return CreateRValueRefType(cloned);
  }

  case TypeNodeKind::BlockPointer: {
    const auto *bp = src->As<const LLDBBlockPointerTypeNode>();
    LLDBQualType cloned(CloneNode(bp->function_type.node),
                        bp->function_type.quals);
    return CreateBlockPointerType(cloned);
  }

  case TypeNodeKind::Array: {
    const auto *a = src->As<const LLDBArrayTypeNode>();
    LLDBQualType cloned_elem(CloneNode(a->element_type.node),
                             a->element_type.quals);
    return CreateArrayType(cloned_elem, a->element_count, a->is_vector);
  }

  case TypeNodeKind::Atomic: {
    const auto *at = src->As<const LLDBAtomicTypeNode>();
    LLDBQualType cloned(CloneNode(at->value_type.node), at->value_type.quals);
    return CreateAtomicType(cloned);
  }

  case TypeNodeKind::MemberPointer: {
    const auto *mp = src->As<const LLDBMemberPointerTypeNode>();
    LLDBQualType cloned_cls(CloneNode(mp->class_type.node), mp->class_type.quals);
    LLDBQualType cloned_pt(CloneNode(mp->pointee_type.node),
                           mp->pointee_type.quals);
    return CreateMemberPointerType(cloned_cls, cloned_pt);
  }

  case TypeNodeKind::Function: {
    const auto *fn = src->As<const LLDBFunctionTypeNode>();
    LLDBQualType cloned_ret(CloneNode(fn->return_type.node),
                            fn->return_type.quals);
    std::vector<LLDBParamNode> cloned_params;
    cloned_params.reserve(fn->params.size());
    for (const auto &p : fn->params) {
      LLDBParamNode cp;
      cp.name = p.name;
      cp.type = LLDBQualType(CloneNode(p.type.node), p.type.quals);
      cloned_params.push_back(std::move(cp));
    }
    return CreateFunctionType(cloned_ret, std::move(cloned_params),
                              fn->is_variadic);
  }

  case TypeNodeKind::Typedef: {
    const auto *td = src->As<const LLDBTypedefTypeNode>();
    LLDBQualType cloned_underlying(CloneNode(td->underlying_type.node),
                                   td->underlying_type.quals);
    return CreateTypedef(td->name, cloned_underlying);
  }

  case TypeNodeKind::Record: {
    const auto *rec = src->As<const LLDBRecordTypeNode>();
    auto *new_rec = CreateRecordType(rec->name, rec->is_union, rec->is_class);
    new_rec->qualified_name          = rec->qualified_name;
    new_rec->is_complete             = rec->is_complete;
    new_rec->is_forcefully_completed = rec->is_forcefully_completed;
    new_rec->language                = rec->language;
    new_rec->byte_size               = rec->byte_size;
    new_rec->alignment_bytes         = rec->alignment_bytes;

    // Clone fields
    for (const auto &f : rec->fields) {
      LLDBFieldNode nf;
      nf.name              = f.name;
      nf.type              = LLDBQualType(CloneNode(f.type.node), f.type.quals);
      nf.bit_offset        = f.bit_offset;
      nf.bitfield_bit_size = f.bitfield_bit_size;
      nf.is_artificial     = f.is_artificial;
      nf.access            = f.access;
      new_rec->fields.push_back(std::move(nf));
    }
    // Clone bases
    for (const auto &b : rec->bases) {
      LLDBBaseClassNode nb;
      nb.type       = LLDBQualType(CloneNode(b.type.node), b.type.quals);
      nb.is_virtual = b.is_virtual;
      nb.bit_offset = b.bit_offset;
      nb.access     = b.access;
      new_rec->bases.push_back(std::move(nb));
    }
    // Clone methods
    for (const auto &m : rec->methods) {
      LLDBMethodNode nm;
      nm.name         = m.name;
      nm.type         = LLDBQualType(CloneNode(m.type.node), m.type.quals);
      nm.is_virtual   = m.is_virtual;
      nm.is_static    = m.is_static;
      nm.is_artificial = m.is_artificial;
      nm.access       = m.access;
      new_rec->methods.push_back(std::move(nm));
    }
    return new_rec;
  }

  case TypeNodeKind::Enum: {
    const auto *en = src->As<const LLDBEnumTypeNode>();
    LLDBQualType cloned_int(CloneNode(en->integer_type.node),
                            en->integer_type.quals);
    auto *new_en = CreateEnumType(en->name, cloned_int, en->is_scoped);
    new_en->is_complete = en->is_complete;
    new_en->enumerators = en->enumerators;
    return new_en;
  }

  case TypeNodeKind::ObjCInterface: {
    const auto *iface = src->As<const LLDBObjCInterfaceTypeNode>();
    auto *new_iface   = CreateObjCInterfaceType(iface->name);
    new_iface->is_complete     = iface->is_complete;
    new_iface->is_forward_decl = iface->is_forward_decl;
    // Clone ivars
    for (const auto &f : iface->ivars) {
      LLDBFieldNode nf;
      nf.name = f.name;
      nf.type = LLDBQualType(CloneNode(f.type.node), f.type.quals);
      nf.bit_offset = f.bit_offset;
      nf.access     = f.access;
      new_iface->ivars.push_back(std::move(nf));
    }
    return new_iface;
  }

  case TypeNodeKind::ObjCObjectPointer: {
    const auto *op = src->As<const LLDBObjCObjectPointerTypeNode>();
    LLDBQualType cloned(CloneNode(op->interface_type.node),
                        op->interface_type.quals);
    return CreateObjCObjectPointerType(cloned);
  }

  case TypeNodeKind::Complex: {
    const auto *cx = src->As<const LLDBComplexTypeNode>();
    return CreateComplexType(CloneNode(cx->element_type));
  }
  }

  llvm_unreachable("Unhandled TypeNodeKind in CloneNode");
}

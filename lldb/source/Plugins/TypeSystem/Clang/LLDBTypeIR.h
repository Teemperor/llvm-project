//===-- LLDBTypeIR.h - Internal type representation -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines LLDB's internal type intermediate representation (IR).
///
/// This replaces direct use of clang::Decl/clang::Type as the primary storage
/// format for debug-info types inside TypeSystemClang. The clang AST is
/// synthesised on demand from these nodes (e.g. for expression evaluation) but
/// is no longer the source of truth.
///
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_TYPESYSTEM_CLANG_LLDBTYPEIR_H
#define LLDB_PLUGINS_TYPESYSTEM_CLANG_LLDBTYPEIR_H

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-defines.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lldb_private {

// ============================================================================
// Forward declarations
// ============================================================================

class LLDBTypeNode;
class LLDBBuiltinTypeNode;
class LLDBPointerTypeNode;
class LLDBLValueReferenceTypeNode;
class LLDBRValueReferenceTypeNode;
class LLDBBlockPointerTypeNode;
class LLDBArrayTypeNode;
class LLDBFunctionTypeNode;
class LLDBRecordTypeNode;
class LLDBEnumTypeNode;
class LLDBTypedefTypeNode;
class LLDBAtomicTypeNode;
class LLDBMemberPointerTypeNode;
class LLDBObjCInterfaceTypeNode;
class LLDBObjCObjectPointerTypeNode;
class LLDBComplexTypeNode;
class LLDBTypeRegistry;
class LLDBNamespaceNode;

// ============================================================================
// TypeNodeKind – discriminator for every node variant
// ============================================================================

enum class TypeNodeKind : uint8_t {
  Builtin,
  Pointer,
  LValueReference,
  RValueReference,
  BlockPointer,
  Array,
  Function,
  Record,
  Enum,
  Typedef,
  Atomic,
  MemberPointer,
  ObjCInterface,
  ObjCObjectPointer,
  Complex,
};

// ============================================================================
// Qualifiers
// ============================================================================

struct LLDBQualifiers {
  bool is_const    : 1;
  bool is_volatile : 1;
  bool is_restrict : 1;

  constexpr LLDBQualifiers()
      : is_const(false), is_volatile(false), is_restrict(false) {}

  bool IsEmpty() const { return !is_const && !is_volatile && !is_restrict; }

  bool operator==(const LLDBQualifiers &o) const {
    return is_const == o.is_const && is_volatile == o.is_volatile &&
           is_restrict == o.is_restrict;
  }
  bool operator!=(const LLDBQualifiers &o) const { return !(*this == o); }
};

// ============================================================================
// LLDBQualType – a (node*, qualifiers) pair, analogous to clang::QualType
// ============================================================================

struct LLDBQualType {
  LLDBTypeNode *node = nullptr;
  LLDBQualifiers quals;

  LLDBQualType() = default;
  /*implicit*/ LLDBQualType(LLDBTypeNode *n) : node(n) {}
  LLDBQualType(LLDBTypeNode *n, LLDBQualifiers q) : node(n), quals(q) {}

  LLDBTypeNode *operator->() const { return node; }
  LLDBTypeNode &operator*() const { return *node; }
  explicit operator bool() const { return node != nullptr; }

  LLDBQualType withConst() const {
    LLDBQualType qt = *this;
    qt.quals.is_const = true;
    return qt;
  }
  LLDBQualType withVolatile() const {
    LLDBQualType qt = *this;
    qt.quals.is_volatile = true;
    return qt;
  }
  LLDBQualType withRestrict() const {
    LLDBQualType qt = *this;
    qt.quals.is_restrict = true;
    return qt;
  }
  LLDBQualType unqualified() const { return LLDBQualType(node); }

  bool isConst()    const { return quals.is_const; }
  bool isVolatile() const { return quals.is_volatile; }
  bool isRestrict() const { return quals.is_restrict; }

  bool operator==(const LLDBQualType &o) const {
    return node == o.node && quals == o.quals;
  }
  bool operator!=(const LLDBQualType &o) const { return !(*this == o); }
};

// ============================================================================
// LLDBTypeNode – abstract base for every IR node
// ============================================================================

class LLDBTypeNode {
public:
  const TypeNodeKind kind;

  explicit LLDBTypeNode(TypeNodeKind k) : kind(k) {}
  virtual ~LLDBTypeNode() = default;

  LLDBTypeNode(const LLDBTypeNode &) = delete;
  LLDBTypeNode &operator=(const LLDBTypeNode &) = delete;

  // Convenience casts (debug-only asserts in non-debug builds are omitted for
  // brevity; callers are responsible for checking kind before casting).
  template <typename T> T *As() { return static_cast<T *>(this); }
  template <typename T> const T *As() const {
    return static_cast<const T *>(this);
  }
};

// ============================================================================
// Builtin type
// ============================================================================

class LLDBBuiltinTypeNode : public LLDBTypeNode {
public:
  lldb::BasicType basic_type;
  uint32_t bit_size;
  bool is_signed;
  bool is_float;

  LLDBBuiltinTypeNode(lldb::BasicType bt, uint32_t bits, bool signed_in,
                      bool float_in)
      : LLDBTypeNode(TypeNodeKind::Builtin), basic_type(bt), bit_size(bits),
        is_signed(signed_in), is_float(float_in) {}
};

// ============================================================================
// Pointer type  T*
// ============================================================================

class LLDBPointerTypeNode : public LLDBTypeNode {
public:
  LLDBQualType pointee;

  explicit LLDBPointerTypeNode(LLDBQualType p)
      : LLDBTypeNode(TypeNodeKind::Pointer), pointee(p) {}
};

// ============================================================================
// Reference types  T&  and  T&&
// ============================================================================

class LLDBLValueReferenceTypeNode : public LLDBTypeNode {
public:
  LLDBQualType pointee;

  explicit LLDBLValueReferenceTypeNode(LLDBQualType p)
      : LLDBTypeNode(TypeNodeKind::LValueReference), pointee(p) {}
};

class LLDBRValueReferenceTypeNode : public LLDBTypeNode {
public:
  LLDBQualType pointee;

  explicit LLDBRValueReferenceTypeNode(LLDBQualType p)
      : LLDBTypeNode(TypeNodeKind::RValueReference), pointee(p) {}
};

// ============================================================================
// Block pointer type  (ObjC/Clang block)  ^T
// ============================================================================

class LLDBBlockPointerTypeNode : public LLDBTypeNode {
public:
  LLDBQualType function_type;

  explicit LLDBBlockPointerTypeNode(LLDBQualType ft)
      : LLDBTypeNode(TypeNodeKind::BlockPointer), function_type(ft) {}
};

// ============================================================================
// Array type  T[N]  or incomplete  T[]
// ============================================================================

class LLDBArrayTypeNode : public LLDBTypeNode {
public:
  LLDBQualType element_type;
  std::optional<uint64_t> element_count; // nullopt means incomplete / flexible
  bool is_vector = false;
  // For VLAs (element_count == nullopt), stores the DWARF type UID so that
  // GetNumChildren can call GetDynamicArrayInfoForUID to get the runtime size.
  lldb::user_id_t vla_dwarf_uid = LLDB_INVALID_UID;

  LLDBArrayTypeNode(LLDBQualType elem, std::optional<uint64_t> count,
                    bool vec = false)
      : LLDBTypeNode(TypeNodeKind::Array), element_type(elem),
        element_count(count), is_vector(vec) {}
};

// ============================================================================
// Data members of a record/ObjC class
// ============================================================================

struct LLDBFieldNode {
  std::string name;
  LLDBQualType type;
  uint64_t bit_offset       = 0;
  uint32_t bitfield_bit_size = 0; // 0 = regular field (not a bitfield)
  bool is_artificial         = false;
  lldb::AccessType access    = lldb::eAccessPublic;
};

// ============================================================================
// Static data members
// ============================================================================

struct LLDBStaticMemberNode {
  std::string name;
  LLDBQualType type;
  lldb::AccessType access = lldb::eAccessPublic;
  /// If present, the member has a DW_AT_const_value and can be folded to a
  /// compile-time constant (e.g. `const static int x = 3;`).
  std::optional<llvm::APSInt> const_int_value;
};

// ============================================================================
// Methods / member functions
// ============================================================================

enum class LLDBRefQualifier : uint8_t { None = 0, LValue, RValue };

struct LLDBMethodNode {
  std::string name;
  std::string asm_label; // FunctionCallLabel::toString() for JIT symbol lookup
  LLDBQualType type; // always a FunctionTypeNode
  bool is_virtual    = false;
  bool is_static     = false;
  bool is_artificial = false;
  bool is_const      = false;
  bool is_volatile   = false;
  LLDBRefQualifier ref_qualifier = LLDBRefQualifier::None;
  lldb::AccessType access = lldb::eAccessPublic;
};

// ============================================================================
// Base-class specification
// ============================================================================

struct LLDBBaseClassNode {
  LLDBQualType type;
  bool is_virtual           = false;
  uint64_t bit_offset       = 0;
  lldb::AccessType access   = lldb::eAccessPublic;
};

// ============================================================================
// Record type  (struct / class / union)
// ============================================================================

class LLDBRecordTypeNode : public LLDBTypeNode {
public:
  std::string name;           ///< unqualified name
  std::string qualified_name; ///< fully qualified (scope::name)
  bool is_union                   = false;
  bool is_class                   = false; ///< true=class, false=struct
  bool is_complete                = false;
  bool is_forcefully_completed    = false;
  bool has_external_storage       = false;
  lldb::LanguageType language     = lldb::eLanguageTypeUnknown;

  std::vector<LLDBFieldNode>        fields;
  std::vector<LLDBBaseClassNode>    bases;
  std::vector<LLDBMethodNode>       methods;
  std::vector<LLDBStaticMemberNode> static_members;
  /// Nested typedef members (e.g. typedef int MemberTypedef inside a class).
  /// Stored as pointers to the corresponding LLDBTypedefTypeNode so that
  /// ClangASTGenerator can cache them and report the correct qualified name.
  std::vector<LLDBTypedefTypeNode *> nested_typedefs;
  /// Nested record types (e.g. struct Outer::Inner). Populated by the DWARF
  /// parser when Inner's parent_record_node is set. Used by CppASTTranslator
  /// to generate nested types inside the parent's Clang DeclContext.
  std::vector<LLDBRecordTypeNode *> nested_records;
  /// If this record is a nested type (e.g. struct Outer::Inner), points to the
  /// enclosing record node. Used by CppASTTranslator to set the correct Clang
  /// DeclContext so that getFullyQualifiedDeclaredContext builds the full NNS.
  LLDBRecordTypeNode *parent_record_node = nullptr;
  /// If this record is directly inside a namespace, points to that namespace
  /// node. Used by CppASTTranslator to set the correct Clang DeclContext.
  LLDBNamespaceNode *parent_namespace_node = nullptr;

  uint64_t byte_size       = 0;
  uint32_t alignment_bytes = 0;

  LLDBRecordTypeNode(llvm::StringRef n, bool is_union_in, bool is_class_in)
      : LLDBTypeNode(TypeNodeKind::Record), name(n), is_union(is_union_in),
        is_class(is_class_in) {}
};

// ============================================================================
// Enum enumerator
// ============================================================================

struct LLDBEnumeratorNode {
  std::string name;
  llvm::APSInt value;
};

// ============================================================================
// Enum type
// ============================================================================

class LLDBEnumTypeNode : public LLDBTypeNode {
public:
  std::string name;
  LLDBQualType integer_type; ///< underlying integer type
  bool is_scoped   = false;
  bool is_complete = false;

  std::vector<LLDBEnumeratorNode> enumerators;

  LLDBEnumTypeNode(llvm::StringRef n, LLDBQualType int_type, bool scoped)
      : LLDBTypeNode(TypeNodeKind::Enum), name(n), integer_type(int_type),
        is_scoped(scoped) {}
};

// ============================================================================
// Function parameter
// ============================================================================

struct LLDBParamNode {
  std::string name;
  LLDBQualType type;
};

// ============================================================================
// Function type
// ============================================================================

class LLDBFunctionTypeNode : public LLDBTypeNode {
public:
  LLDBQualType return_type;
  std::vector<LLDBParamNode> params;
  bool is_variadic = false;

  LLDBFunctionTypeNode(LLDBQualType ret, std::vector<LLDBParamNode> ps,
                       bool variadic)
      : LLDBTypeNode(TypeNodeKind::Function), return_type(ret),
        params(std::move(ps)), is_variadic(variadic) {}
};

// ============================================================================
// Typedef / type alias
// ============================================================================

class LLDBTypedefTypeNode : public LLDBTypeNode {
public:
  std::string name;
  std::string qualified_name; ///< fully qualified (e.g. "Outer::MyTypedef")
  LLDBQualType underlying_type;
  LLDBTypeNode *parent_node = nullptr; ///< parent class/struct if nested
  bool is_ptrauth = false; ///< true if this typedef wraps a ptrauth-qualified type

  LLDBTypedefTypeNode(llvm::StringRef n, LLDBQualType underlying)
      : LLDBTypeNode(TypeNodeKind::Typedef), name(n),
        underlying_type(underlying) {}
};

// ============================================================================
// Atomic type  _Atomic(T)
// ============================================================================

class LLDBAtomicTypeNode : public LLDBTypeNode {
public:
  LLDBQualType value_type;

  explicit LLDBAtomicTypeNode(LLDBQualType vt)
      : LLDBTypeNode(TypeNodeKind::Atomic), value_type(vt) {}
};

// ============================================================================
// Complex type  _Complex T
// ============================================================================

class LLDBComplexTypeNode : public LLDBTypeNode {
public:
  LLDBTypeNode *element_type; ///< element type (e.g. int for _Complex int)

  explicit LLDBComplexTypeNode(LLDBTypeNode *elem)
      : LLDBTypeNode(TypeNodeKind::Complex), element_type(elem) {}
};

// ============================================================================
// Member pointer type  T C::*
// ============================================================================

class LLDBMemberPointerTypeNode : public LLDBTypeNode {
public:
  LLDBQualType class_type;
  LLDBQualType pointee_type;

  LLDBMemberPointerTypeNode(LLDBQualType cls, LLDBQualType pt)
      : LLDBTypeNode(TypeNodeKind::MemberPointer), class_type(cls),
        pointee_type(pt) {}
};

// ============================================================================
// ObjC interface type  @interface Foo
// ============================================================================

struct LLDBObjCPropertyNode {
  std::string name;
  LLDBQualType type;
};

class LLDBObjCInterfaceTypeNode : public LLDBTypeNode {
public:
  std::string name;
  bool is_complete     = false;
  bool is_forward_decl = false;

  std::vector<LLDBFieldNode>          ivars;
  std::vector<LLDBMethodNode>         methods;
  std::vector<LLDBObjCPropertyNode>   properties;
  LLDBQualType superclass; ///< invalid if none

  explicit LLDBObjCInterfaceTypeNode(llvm::StringRef n)
      : LLDBTypeNode(TypeNodeKind::ObjCInterface), name(n) {}
};

// ============================================================================
// ObjC object pointer  Foo* / id / Class
// ============================================================================

class LLDBObjCObjectPointerTypeNode : public LLDBTypeNode {
public:
  LLDBQualType interface_type; ///< may be invalid (id, Class)

  explicit LLDBObjCObjectPointerTypeNode(LLDBQualType iface)
      : LLDBTypeNode(TypeNodeKind::ObjCObjectPointer), interface_type(iface) {}
};

// ============================================================================
// Namespace node (for decl contexts)
// ============================================================================
class LLDBNamespaceNode {
public:
  std::string name;
  std::string qualified_name; // e.g. "A::B"
  LLDBNamespaceNode *parent = nullptr; // nullptr = translation-unit level
  bool is_inline = false;

  LLDBNamespaceNode(llvm::StringRef n, llvm::StringRef qn,
                    LLDBNamespaceNode *p, bool inl = false)
      : name(n), qualified_name(qn), parent(p), is_inline(inl) {}
};

// ============================================================================
// LLDBTypeRegistry
//
// Owns *all* IR nodes for a single TypeSystemClang instance. Nodes have stable
// addresses for the lifetime of the registry. No ASTImporter is needed: to
// transfer a type from one TypeSystem to another, clone the IR node (see
// CloneInto).
// ============================================================================

class LLDBTypeRegistry {
public:
  LLDBTypeRegistry()  = default;
  ~LLDBTypeRegistry() = default;

  LLDBTypeRegistry(const LLDBTypeRegistry &) = delete;
  LLDBTypeRegistry &operator=(const LLDBTypeRegistry &) = delete;

  // ---- Builtin types -------------------------------------------------------

  /// Returns a cached builtin node or creates a new one.
  LLDBBuiltinTypeNode *GetOrCreateBuiltin(lldb::BasicType basic_type,
                                          uint32_t bit_size, bool is_signed,
                                          bool is_float);

  /// Creates a fresh (uncached) builtin node for _BitInt(N) types.
  /// Unlike GetOrCreateBuiltin, each call creates a new node so different
  /// _BitInt widths each get their own node.
  LLDBBuiltinTypeNode *CreateBitIntType(uint32_t bit_size, bool is_signed);

  // ---- Derived types -------------------------------------------------------

  LLDBPointerTypeNode *CreatePointerType(LLDBQualType pointee);
  LLDBLValueReferenceTypeNode *CreateLValueRefType(LLDBQualType pointee);
  LLDBRValueReferenceTypeNode *CreateRValueRefType(LLDBQualType pointee);
  LLDBBlockPointerTypeNode   *CreateBlockPointerType(LLDBQualType fn_type);
  LLDBArrayTypeNode          *CreateArrayType(LLDBQualType elem,
                                              std::optional<uint64_t> count,
                                              bool is_vector = false);
  LLDBAtomicTypeNode         *CreateAtomicType(LLDBQualType value_type);
  LLDBComplexTypeNode        *CreateComplexType(LLDBTypeNode *element_type);
  LLDBMemberPointerTypeNode  *CreateMemberPointerType(LLDBQualType cls,
                                                      LLDBQualType pointee);

  // ---- Structural types ----------------------------------------------------

  LLDBRecordTypeNode *CreateRecordType(llvm::StringRef name, bool is_union,
                                       bool is_class);
  LLDBEnumTypeNode   *CreateEnumType(llvm::StringRef name, LLDBQualType int_type,
                                     bool is_scoped);
  LLDBFunctionTypeNode *CreateFunctionType(LLDBQualType ret,
                                           std::vector<LLDBParamNode> params,
                                           bool is_variadic);
  LLDBTypedefTypeNode  *CreateTypedef(llvm::StringRef name,
                                      LLDBQualType underlying);
  LLDBObjCInterfaceTypeNode    *CreateObjCInterfaceType(llvm::StringRef name);
  LLDBObjCObjectPointerTypeNode *CreateObjCObjectPointerType(LLDBQualType iface);

  // ---- Namespace nodes (for decl contexts) ---------------------------------
  LLDBNamespaceNode *GetOrCreateNamespace(llvm::StringRef qualified_name,
                                          LLDBNamespaceNode *parent,
                                          bool is_inline = false);
  LLDBNamespaceNode *FindNamespace(llvm::StringRef qualified_name) const;

  // ---- Clone a node from another registry ----------------------------------
  // (replaces ASTImporter: copy the source-of-truth IR node instead of
  // shuffling clang AST nodes between ASTContext instances)
  LLDBTypeNode *CloneNode(const LLDBTypeNode *src);

  // ---- Iteration -----------------------------------------------------------

  size_t GetNodeCount() const { return m_nodes.size(); }
  llvm::ArrayRef<std::unique_ptr<LLDBTypeNode>> GetAllNodes() const {
    return m_nodes;
  }

private:
  template <typename NodeType, typename... Args>
  NodeType *Alloc(Args &&...args) {
    auto node = std::make_unique<NodeType>(std::forward<Args>(args)...);
    NodeType *ptr = node.get();
    m_nodes.push_back(std::move(node));
    return ptr;
  }

  std::vector<std::unique_ptr<LLDBTypeNode>> m_nodes;

  // Cache: BasicType → node (there is at most one node per BasicType)
  llvm::SmallVector<LLDBBuiltinTypeNode *, 64> m_builtin_cache;

  // Namespace nodes: key = qualified_name
  llvm::StringMap<std::unique_ptr<LLDBNamespaceNode>> m_namespace_nodes;
};

// ============================================================================
// Utility helpers
// ============================================================================

/// Walk through typedef chains and return the underlying non-typedef node.
inline LLDBTypeNode *LLDBTypeDesugar(LLDBTypeNode *node) {
  while (node && node->kind == TypeNodeKind::Typedef)
    node = node->As<LLDBTypedefTypeNode>()->underlying_type.node;
  return node;
}

inline const LLDBTypeNode *LLDBTypeDesugar(const LLDBTypeNode *node) {
  while (node && node->kind == TypeNodeKind::Typedef)
    node = node->As<const LLDBTypedefTypeNode>()->underlying_type.node;
  return node;
}

/// Returns true when \p node (after desugaring) is a record type.
inline bool LLDBTypeIsRecord(const LLDBTypeNode *node) {
  return node && LLDBTypeDesugar(node)->kind == TypeNodeKind::Record;
}

/// Returns true when \p node (after desugaring) is an ObjC interface.
inline bool LLDBTypeIsObjCInterface(const LLDBTypeNode *node) {
  return node && LLDBTypeDesugar(node)->kind == TypeNodeKind::ObjCInterface;
}

} // namespace lldb_private

#endif // LLDB_PLUGINS_TYPESYSTEM_CLANG_LLDBTYPEIR_H

//===-- BuiltinTypes.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BUILTIN_TYPES_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BUILTIN_TYPES_H

#include "Type.h"
#include "LanguageOpts.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace lldb_private {
namespace clike_typesystem {

class IdentifierMap;

/// Enumerates the builtin types of C, C++ and Objective-C.
enum class BuiltinKind : uint8_t {
  Void,
  Bool,
  Char,
  SignedChar,
  UnsignedChar,
  WCharT,
  Char8,
  Char16,
  Char32,
  Short,
  UnsignedShort,
  Int,
  UnsignedInt,
  Long,
  UnsignedLong,
  LongLong,
  UnsignedLongLong,
  Int128,
  UnsignedInt128,
  Float,
  Double,
  LongDouble,
  NullPtr,
  NumKinds
};

class BuiltinType : public llvm::RTTIExtends<BuiltinType, ByteSizedType<Type>> {
public:
  static char ID;

  // A builtin is a named type (e.g. "int", "long unsigned int"); Type itself
  // holds no name storage (see its class comment), so this is where it lives.
  Identifier GetName() const override { return m_name; }
  void SetName(Identifier name) { m_name = name; }

  void SetEncoding(lldb::Encoding encoding) { m_encoding = encoding; }
  lldb::Encoding GetEncoding() const override { return m_encoding; }

  /// The canonical builtin kind, if this instance is one of the shared
  /// canonical builtin types (see KnownBuiltinTypes). Bespoke builtin types
  /// created from raw DWARF attributes have no known kind.
  void SetBuiltinKind(BuiltinKind kind) { m_kind = kind; }
  std::optional<BuiltinKind> GetBuiltinKind() const { return m_kind; }

  /// The display format for values of this type. Unlike the encoding, this
  /// cannot always be derived from the encoding alone (e.g. char and bool
  /// share the integer encodings), so it is stored explicitly.
  void SetFormat(lldb::Format format) { m_format = format; }
  lldb::Format GetFormat() const override { return m_format; }

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassBuiltin;
  }

  uint32_t GetTypeInfo() const override;

  /// The lldb::BasicType this builtin corresponds to, or eBasicTypeInvalid for
  /// a bespoke builtin created from raw DWARF attributes (which has no known
  /// kind). This is the inverse of KnownBuiltinTypes::KindForBasicType.
  lldb::BasicType GetBasicTypeEnumeration() const;

  /// True for the narrow character types (char/signed char/unsigned char, but
  /// not wchar_t), matching clang's isCharType(). Identified by the display
  /// format and size rather than by kind, so a bespoke builtin reached through
  /// another Context (e.g. an expression result mapped into the scratch
  /// TypeSystemClike) is still recognized.
  bool IsChar() const {
    return m_format == lldb::eFormatChar && GetByteSize().value_or(0) == 1;
  }

  /// True for `void`: the one builtin with no encoding at all. Used to
  /// recognize a `void *` pointee. Also accepts a bespoke `void` built from raw
  /// DWARF attributes (which carries no BuiltinKind), since a producer that
  /// spells the size differently than KnownBuiltinTypes::Match expects still
  /// means void.
  bool IsVoid() const {
    if (m_kind)
      return *m_kind == BuiltinKind::Void;
    return m_encoding == lldb::eEncodingInvalid &&
           m_name.GetName() == "void";
  }

  /// True for a builtin the C++ integral promotions apply to ([conv.prom]):
  /// bool, the character types, and the integer types whose conversion rank is
  /// below int.
  bool IsPromotableInteger() const;

private:
  Identifier m_name;
  lldb::Encoding m_encoding = lldb::eEncodingInvalid;
  lldb::Format m_format = lldb::eFormatDefault;
  std::optional<BuiltinKind> m_kind;
};

/// Owns one canonical BuiltinType instance for every C/C++/Objective-C builtin
/// type (see BuiltinKind). Symbol readers map their source-format base types
/// onto these shared instances via Match(), rather than creating a fresh type
/// each time; each instance therefore carries the correct encoding, display
/// format and type info for its kind in one place.
class KnownBuiltinTypes {
public:
  KnownBuiltinTypes(const LanguageOpts &opts, IdentifierMap &identifiers);

  /// The canonical instance for a specific builtin kind.
  BuiltinType *Get(BuiltinKind kind) const {
    return m_by_kind[static_cast<size_t>(kind)];
  }

  /// Returns the canonical builtin type matching the given encoding, byte size
  /// and (one of the) known spelling(s), or nullptr if none of the enumerated
  /// builtin types matches. A null result means the caller should fall back to
  /// creating a bespoke type from the raw attributes.
  BuiltinType *Match(llvm::StringRef name, lldb::Encoding encoding,
                     std::optional<uint64_t> byte_size) const;

  /// Returns the canonical builtin type whose (canonical or alternative)
  /// spelling equals \p name, or nullptr if no enumerated builtin is spelled
  /// that way. Used to answer type lookups by name (e.g. FindTypes).
  BuiltinType *MatchByName(llvm::StringRef name) const;

  /// The builtin kind corresponding to \p basic_type, or std::nullopt for a
  /// BasicType this type system doesn't model as a builtin (e.g.
  /// eBasicTypeObjCID, which is a typedef over a pointer). The inverse of
  /// BuiltinType::GetBasicTypeEnumeration.
  static std::optional<BuiltinKind> KindForBasicType(lldb::BasicType basic_type);

  /// The builtin kind for a value of \p encoding that is \p bit_size bits
  /// wide, or std::nullopt when no builtin has that combination. Backs
  /// TypeSystem::GetBuiltinTypeForEncodingAndBitSize, whose callers (register
  /// contexts, data formatters, runtime plugins) know only a raw
  /// encoding+width and want the type that carries it.
  ///
  /// Unlike Match(), this takes no spelling, so where several builtins share a
  /// width one has to be preferred; see the table in BuiltinTypes.cpp for which
  /// and why. The widths are the fixed ones the callers actually ask for, not
  /// the target's -- a caller asking for "the 32-bit signed type" wants `int`
  /// on every supported target.
  static std::optional<BuiltinKind>
  KindForEncodingAndBitSize(lldb::Encoding encoding, size_t bit_size);

private:
  // Backing storage for the canonical instances; index has no meaning.
  std::vector<std::unique_ptr<BuiltinType>> m_storage;
  // Canonical instance for each BuiltinKind, indexed by the enum value.
  std::array<BuiltinType *, static_cast<size_t>(BuiltinKind::NumKinds)>
      m_by_kind{};
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BUILTIN_TYPES_H

//===-- BuiltinTypes.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BuiltinTypes.h"
#include "Identifier.h"

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

// LLVM RTTI discriminator; see Type.cpp for details.
char lldb_private::clike_typesystem::BuiltinType::ID = 0;

uint32_t BuiltinType::GetTypeInfo() const {
  // std::nullptr_t is neither a scalar nor an integer (matching Clang, whose
  // GetTypeInfo returns 0 for it), even though its representation uses an
  // unsigned pointer-width encoding. It still has a value.
  if (m_kind == BuiltinKind::NullPtr)
    return lldb::eTypeHasValue;
  uint32_t info = lldb::eTypeIsBuiltIn | lldb::eTypeHasValue;
  switch (m_encoding) {
  case lldb::eEncodingSint:
    info |= lldb::eTypeIsScalar | lldb::eTypeIsInteger | lldb::eTypeIsSigned;
    break;
  case lldb::eEncodingUint:
    info |= lldb::eTypeIsScalar | lldb::eTypeIsInteger;
    break;
  case lldb::eEncodingIEEE754:
    info |= lldb::eTypeIsScalar | lldb::eTypeIsFloat | lldb::eTypeIsSigned;
    break;
  default:
    break;
  }
  return info;
}

namespace {
/// Static description of one builtin kind. Byte sizes are target-dependent and
/// therefore not part of this table; they are computed from the triple.
struct BuiltinDesc {
  BuiltinKind kind;
  lldb::Encoding encoding;
  lldb::Format format;
  /// The lldb::BasicType this kind corresponds to. Keeping it in the same table
  /// as everything else keeps the two mapping directions (BuiltinKind ->
  /// BasicType and back) from drifting apart.
  lldb::BasicType basic_type;
  /// True if the C++ integral promotions ([conv.prom]) apply to this kind:
  /// bool, the character types, and the integer types ranked below int.
  bool is_promotable;
  /// Known spellings for this type. The first is the canonical name; the rest
  /// are alternative spellings different compilers emit (e.g. GCC writes
  /// "long unsigned int" where Clang writes "unsigned long"). Unused slots are
  /// null.
  std::array<const char *, 3> spellings;
};

// clang-format off
constexpr BuiltinDesc kDescs[] = {
    {BuiltinKind::Void,             lldb::eEncodingInvalid, lldb::eFormatDefault,   lldb::eBasicTypeVoid,             false, {"void", nullptr, nullptr}},
    {BuiltinKind::Bool,             lldb::eEncodingUint,    lldb::eFormatBoolean,   lldb::eBasicTypeBool,             true,  {"bool", "_Bool", nullptr}},
    {BuiltinKind::Char,             lldb::eEncodingSint,    lldb::eFormatChar,      lldb::eBasicTypeChar,             true,  {"char", nullptr, nullptr}},
    {BuiltinKind::SignedChar,       lldb::eEncodingSint,    lldb::eFormatChar,      lldb::eBasicTypeSignedChar,       true,  {"signed char", nullptr, nullptr}},
    {BuiltinKind::UnsignedChar,     lldb::eEncodingUint,    lldb::eFormatChar,      lldb::eBasicTypeUnsignedChar,     true,  {"unsigned char", nullptr, nullptr}},
    {BuiltinKind::WCharT,           lldb::eEncodingSint,    lldb::eFormatChar,      lldb::eBasicTypeWChar,            true,  {"wchar_t", nullptr, nullptr}},
    {BuiltinKind::Char8,            lldb::eEncodingUint,    lldb::eFormatUnicode8,  lldb::eBasicTypeChar8,            true,  {"char8_t", nullptr, nullptr}},
    {BuiltinKind::Char16,           lldb::eEncodingUint,    lldb::eFormatUnicode16, lldb::eBasicTypeChar16,           true,  {"char16_t", nullptr, nullptr}},
    {BuiltinKind::Char32,           lldb::eEncodingUint,    lldb::eFormatUnicode32, lldb::eBasicTypeChar32,           true,  {"char32_t", nullptr, nullptr}},
    {BuiltinKind::Short,            lldb::eEncodingSint,    lldb::eFormatDecimal,   lldb::eBasicTypeShort,            true,  {"short", "short int", nullptr}},
    {BuiltinKind::UnsignedShort,    lldb::eEncodingUint,    lldb::eFormatUnsigned,  lldb::eBasicTypeUnsignedShort,    true,  {"unsigned short", "short unsigned int", "unsigned short int"}},
    {BuiltinKind::Int,              lldb::eEncodingSint,    lldb::eFormatDecimal,   lldb::eBasicTypeInt,              false, {"int", nullptr, nullptr}},
    {BuiltinKind::UnsignedInt,      lldb::eEncodingUint,    lldb::eFormatUnsigned,  lldb::eBasicTypeUnsignedInt,      false, {"unsigned int", "unsigned", nullptr}},
    {BuiltinKind::Long,             lldb::eEncodingSint,    lldb::eFormatDecimal,   lldb::eBasicTypeLong,             false, {"long", "long int", nullptr}},
    {BuiltinKind::UnsignedLong,     lldb::eEncodingUint,    lldb::eFormatUnsigned,  lldb::eBasicTypeUnsignedLong,     false, {"unsigned long", "long unsigned int", "unsigned long int"}},
    {BuiltinKind::LongLong,         lldb::eEncodingSint,    lldb::eFormatDecimal,   lldb::eBasicTypeLongLong,         false, {"long long", "long long int", nullptr}},
    {BuiltinKind::UnsignedLongLong, lldb::eEncodingUint,    lldb::eFormatUnsigned,  lldb::eBasicTypeUnsignedLongLong, false, {"unsigned long long", "long long unsigned int", "unsigned long long int"}},
    {BuiltinKind::Int128,           lldb::eEncodingSint,    lldb::eFormatDecimal,   lldb::eBasicTypeInt128,           false, {"__int128", nullptr, nullptr}},
    {BuiltinKind::UnsignedInt128,   lldb::eEncodingUint,    lldb::eFormatUnsigned,  lldb::eBasicTypeUnsignedInt128,   false, {"unsigned __int128", "__int128 unsigned", nullptr}},
    {BuiltinKind::Float,            lldb::eEncodingIEEE754, lldb::eFormatFloat,     lldb::eBasicTypeFloat,            false, {"float", nullptr, nullptr}},
    {BuiltinKind::Double,           lldb::eEncodingIEEE754, lldb::eFormatFloat,     lldb::eBasicTypeDouble,           false, {"double", nullptr, nullptr}},
    {BuiltinKind::LongDouble,       lldb::eEncodingIEEE754, lldb::eFormatFloat,     lldb::eBasicTypeLongDouble,       false, {"long double", nullptr, nullptr}},
    {BuiltinKind::NullPtr,          lldb::eEncodingUint,    lldb::eFormatHex,       lldb::eBasicTypeNullPtr,          false, {"std::nullptr_t", "decltype(nullptr)", "nullptr_t"}},
};
// clang-format on

static_assert(std::size(kDescs) == static_cast<size_t>(BuiltinKind::NumKinds),
              "every BuiltinKind needs exactly one description");

/// The description of \p kind. kDescs is indexed by the enum value.
const BuiltinDesc &DescFor(BuiltinKind kind) {
  return kDescs[static_cast<size_t>(kind)];
}

/// kDescs is indexed by BuiltinKind, so it must list the kinds in enum order.
constexpr bool DescsAreInKindOrder() {
  for (size_t i = 0; i != std::size(kDescs); ++i)
    if (static_cast<size_t>(kDescs[i].kind) != i)
      return false;
  return true;
}
static_assert(DescsAreInKindOrder(),
              "kDescs must list the kinds in BuiltinKind order");

/// The size (in bytes) of a builtin type on the given target, or nullopt for
/// types without a size (void). The target-dependent sizes come from Clang's
/// target knowledge (see LanguageOpts); if the value here disagrees with what
/// the symbol reader reports, Match() falls through and a bespoke type is
/// created with the reported size instead.
std::optional<uint64_t> ByteSizeFor(BuiltinKind kind,
                                    const LanguageOpts::BuiltinSizes &sizes) {
  switch (kind) {
  case BuiltinKind::Void:
    return std::nullopt;
  case BuiltinKind::Char:
  case BuiltinKind::SignedChar:
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::Char8:
    return 1;
  case BuiltinKind::Bool:
    return sizes.bool_size;
  case BuiltinKind::Char16:
    return sizes.char16_size;
  case BuiltinKind::Char32:
    return sizes.char32_size;
  case BuiltinKind::WCharT:
    return sizes.wchar_size;
  case BuiltinKind::Short:
  case BuiltinKind::UnsignedShort:
    return sizes.short_size;
  case BuiltinKind::Int:
  case BuiltinKind::UnsignedInt:
    return sizes.int_size;
  case BuiltinKind::Long:
  case BuiltinKind::UnsignedLong:
    return sizes.long_size;
  case BuiltinKind::LongLong:
  case BuiltinKind::UnsignedLongLong:
    return sizes.long_long_size;
  case BuiltinKind::Int128:
  case BuiltinKind::UnsignedInt128:
    return sizes.int128_size;
  case BuiltinKind::Float:
    return sizes.float_size;
  case BuiltinKind::Double:
    return sizes.double_size;
  case BuiltinKind::LongDouble:
    return sizes.long_double_size;
  case BuiltinKind::NullPtr:
    return sizes.pointer_size;
  case BuiltinKind::NumKinds:
    break;
  }
  return std::nullopt;
}
} // namespace

lldb::BasicType BuiltinType::GetBasicTypeEnumeration() const {
  // A bespoke builtin created from raw DWARF attributes has no known kind and
  // therefore no basic type.
  if (!m_kind)
    return lldb::eBasicTypeInvalid;
  return DescFor(*m_kind).basic_type;
}

bool BuiltinType::IsPromotableInteger() const {
  return m_kind && DescFor(*m_kind).is_promotable;
}

namespace {
/// One (encoding, byte size) -> kind entry for KindForEncodingAndBitSize.
struct EncodingSizeDesc {
  lldb::Encoding encoding;
  uint64_t byte_size;
  BuiltinKind kind;
};

// The widths here are fixed, not target-derived: a caller that asks for "the
// 32-bit signed type" wants `int` on every target this supports.
//
// Where several builtins share a width, the entry names the preferred one:
//   - 1-byte signed is `signed char`, not `char` (which is also 1 byte but has
//     implementation-defined signedness).
//   - 8-byte int is `long long`, not `long`; 16-byte float is `long double`.
// NB: TypeSystemClang's GetBuiltinTypeForEncodingAndBitSize resolves these ties
// differently -- it walks char/short/int/long/long long in order and stops at
// the first whose *target* width matches, so on LP64 it answers `long` for
// 8 bytes where this answers `long long`. That divergence predates this table;
// it is preserved here rather than silently changed.
constexpr EncodingSizeDesc kEncodingSizes[] = {
    {lldb::eEncodingSint, 1, BuiltinKind::SignedChar},
    {lldb::eEncodingSint, 2, BuiltinKind::Short},
    {lldb::eEncodingSint, 4, BuiltinKind::Int},
    {lldb::eEncodingSint, 8, BuiltinKind::LongLong},
    {lldb::eEncodingSint, 16, BuiltinKind::Int128},
    {lldb::eEncodingUint, 1, BuiltinKind::UnsignedChar},
    {lldb::eEncodingUint, 2, BuiltinKind::UnsignedShort},
    {lldb::eEncodingUint, 4, BuiltinKind::UnsignedInt},
    {lldb::eEncodingUint, 8, BuiltinKind::UnsignedLongLong},
    {lldb::eEncodingUint, 16, BuiltinKind::UnsignedInt128},
    {lldb::eEncodingIEEE754, 4, BuiltinKind::Float},
    {lldb::eEncodingIEEE754, 8, BuiltinKind::Double},
    {lldb::eEncodingIEEE754, 16, BuiltinKind::LongDouble},
};
} // namespace

std::optional<BuiltinKind>
KnownBuiltinTypes::KindForEncodingAndBitSize(lldb::Encoding encoding,
                                             size_t bit_size) {
  const uint64_t byte_size = (bit_size + 7) / 8;
  for (const EncodingSizeDesc &desc : kEncodingSizes)
    if (desc.encoding == encoding && desc.byte_size == byte_size)
      return desc.kind;
  return std::nullopt;
}

std::optional<BuiltinKind>
KnownBuiltinTypes::KindForBasicType(lldb::BasicType basic_type) {
  for (const BuiltinDesc &desc : kDescs)
    if (desc.basic_type == basic_type)
      return desc.kind;
  return std::nullopt;
}

KnownBuiltinTypes::KnownBuiltinTypes(const LanguageOpts &opts,
                                     IdentifierMap &identifiers) {
  const LanguageOpts::BuiltinSizes &sizes = opts.GetBuiltinSizes();
  m_storage.reserve(std::size(kDescs));
  for (const BuiltinDesc &desc : kDescs) {
    auto type = std::make_unique<BuiltinType>();
    // Spellings are string literals, so their storage outlives everything.
    type->SetName(identifiers.getWithStaticStorageStr(desc.spellings[0]));
    type->SetEncoding(desc.encoding);
    type->SetFormat(desc.format);
    type->SetBuiltinKind(desc.kind);
    type->SetByteSize(ByteSizeFor(desc.kind, sizes));
    m_by_kind[static_cast<size_t>(desc.kind)] = type.get();
    m_storage.push_back(std::move(type));
  }
}

BuiltinType *KnownBuiltinTypes::Match(llvm::StringRef name,
                                      lldb::Encoding encoding,
                                      std::optional<uint64_t> byte_size) const {
  for (const BuiltinDesc &desc : kDescs) {
    if (desc.encoding != encoding)
      continue;
    BuiltinType *type = Get(desc.kind);
    if (type->GetByteSize() != byte_size)
      continue;
    for (const char *spelling : desc.spellings) {
      if (spelling && name == spelling)
        return type;
    }
  }
  return nullptr;
}

BuiltinType *KnownBuiltinTypes::MatchByName(llvm::StringRef name) const {
  for (const BuiltinDesc &desc : kDescs)
    for (const char *spelling : desc.spellings)
      if (spelling && name == spelling)
        return Get(desc.kind);
  return nullptr;
}

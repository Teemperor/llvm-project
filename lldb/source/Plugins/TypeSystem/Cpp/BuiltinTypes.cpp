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
using namespace lldb_private::cpp_typesystem;

// LLVM RTTI discriminator; see Type.cpp for details.
char lldb_private::cpp_typesystem::BuiltinType::ID = 0;

namespace {
/// Static description of one builtin kind. Byte sizes are target-dependent and
/// therefore not part of this table; they are computed from the triple.
struct BuiltinDesc {
  BuiltinKind kind;
  lldb::Encoding encoding;
  lldb::Format format;
  /// Known spellings for this type. The first is the canonical name; the rest
  /// are alternative spellings different compilers emit (e.g. GCC writes
  /// "long unsigned int" where Clang writes "unsigned long"). Unused slots are
  /// null.
  std::array<const char *, 3> spellings;
};

// clang-format off
constexpr BuiltinDesc kDescs[] = {
    {BuiltinKind::Void,             lldb::eEncodingInvalid, lldb::eFormatDefault,   {"void", nullptr, nullptr}},
    {BuiltinKind::Bool,             lldb::eEncodingUint,    lldb::eFormatBoolean,   {"bool", "_Bool", nullptr}},
    {BuiltinKind::Char,             lldb::eEncodingSint,    lldb::eFormatChar,      {"char", nullptr, nullptr}},
    {BuiltinKind::SignedChar,       lldb::eEncodingSint,    lldb::eFormatChar,      {"signed char", nullptr, nullptr}},
    {BuiltinKind::UnsignedChar,     lldb::eEncodingUint,    lldb::eFormatChar,      {"unsigned char", nullptr, nullptr}},
    {BuiltinKind::WCharT,           lldb::eEncodingSint,    lldb::eFormatChar,      {"wchar_t", nullptr, nullptr}},
    {BuiltinKind::Char8,            lldb::eEncodingUint,    lldb::eFormatUnicode8,  {"char8_t", nullptr, nullptr}},
    {BuiltinKind::Char16,           lldb::eEncodingUint,    lldb::eFormatUnicode16, {"char16_t", nullptr, nullptr}},
    {BuiltinKind::Char32,           lldb::eEncodingUint,    lldb::eFormatUnicode32, {"char32_t", nullptr, nullptr}},
    {BuiltinKind::Short,            lldb::eEncodingSint,    lldb::eFormatDecimal,   {"short", "short int", nullptr}},
    {BuiltinKind::UnsignedShort,    lldb::eEncodingUint,    lldb::eFormatUnsigned,  {"unsigned short", "short unsigned int", "unsigned short int"}},
    {BuiltinKind::Int,              lldb::eEncodingSint,    lldb::eFormatDecimal,   {"int", nullptr, nullptr}},
    {BuiltinKind::UnsignedInt,      lldb::eEncodingUint,    lldb::eFormatUnsigned,  {"unsigned int", "unsigned", nullptr}},
    {BuiltinKind::Long,             lldb::eEncodingSint,    lldb::eFormatDecimal,   {"long", "long int", nullptr}},
    {BuiltinKind::UnsignedLong,     lldb::eEncodingUint,    lldb::eFormatUnsigned,  {"unsigned long", "long unsigned int", "unsigned long int"}},
    {BuiltinKind::LongLong,         lldb::eEncodingSint,    lldb::eFormatDecimal,   {"long long", "long long int", nullptr}},
    {BuiltinKind::UnsignedLongLong, lldb::eEncodingUint,    lldb::eFormatUnsigned,  {"unsigned long long", "long long unsigned int", "unsigned long long int"}},
    {BuiltinKind::Int128,           lldb::eEncodingSint,    lldb::eFormatDecimal,   {"__int128", nullptr, nullptr}},
    {BuiltinKind::UnsignedInt128,   lldb::eEncodingUint,    lldb::eFormatUnsigned,  {"unsigned __int128", "__int128 unsigned", nullptr}},
    {BuiltinKind::Float,            lldb::eEncodingIEEE754, lldb::eFormatFloat,     {"float", nullptr, nullptr}},
    {BuiltinKind::Double,           lldb::eEncodingIEEE754, lldb::eFormatFloat,     {"double", nullptr, nullptr}},
    {BuiltinKind::LongDouble,       lldb::eEncodingIEEE754, lldb::eFormatFloat,     {"long double", nullptr, nullptr}},
};
// clang-format on

static_assert(std::size(kDescs) == static_cast<size_t>(BuiltinKind::NumKinds),
              "every BuiltinKind needs exactly one description");

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
  case BuiltinKind::NumKinds:
    break;
  }
  return std::nullopt;
}
} // namespace

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

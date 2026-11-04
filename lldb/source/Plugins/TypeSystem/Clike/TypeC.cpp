//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeC.h"

using namespace lldb_private::clike;

// Storage for the LLVM RTTI discriminators. The addresses (not the values) are
// what identify each class, so the initializer is irrelevant.
char TypedefType::ID = 0;
char CVQualifiedType::ID = 0;

std::string
CVQualifiedType::GetDisplayName(const DisplayNameOptions &opts) const {
  std::string qualifier;
  if (m_is_const)
    qualifier += "const";
  if (m_is_volatile)
    qualifier += qualifier.empty() ? "volatile" : " volatile";
  std::string underlying = GetUnderlyingType()->GetDisplayName(opts);
  return qualifier + (qualifier.empty() ? "" : " ") + underlying;
}

std::string CVQualifiedType::GetCanonicalName(bool base_only) const {
  // The cv-qualifiers prefix the unqualified name, but only if the underlying
  // type spells them (see Type::SpellsCVQualifiersInCanonicalName).
  std::string underlying_name = GetUnderlyingType()->GetCanonicalName(base_only);
  if (!GetUnderlyingType()->SpellsCVQualifiersInCanonicalName())
    return underlying_name;
  std::string result;
  if (m_is_const)
    result += "const ";
  if (m_is_volatile)
    result += "volatile ";
  result += underlying_name;
  return result;
}

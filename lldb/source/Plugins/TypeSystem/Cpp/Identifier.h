//===-- CppNodes.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_IDENTIFIER_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_IDENTIFIER_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"

#include "llvm/ADT/SmallVector.h"

#include <vector>

namespace lldb_private {
namespace cpp_typesystem {

/// Turns strings into unique IDs.
class IdentifierMap {
public:
    Identifier get(StringRef name) {
        // TODO: Internalize this into a StringMap.
    }
private:
};

/// Represents an unqualified name.
// E.g. `string` or `std`, but *NOT* `std::string`.
class Identifier {
public:
private:
    // This is stored in a IdentifierMap.
    StringRef m_name;
};

/// Represents a fully qualified name such as `std::string`.
class QualifiedName {
public:
private:
    std::vector<Identifier> m_identifiers;
};

}
}

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_IDENTIFIER_H
//===-- CppNodes.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_DECLS_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_DECLS_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"

#include "llvm/ADT/SmallVector.h"

#include <vector>

namespace lldb_private {
namespace cpp_typesystem {

class CppContext;

class Identifier {

};

class TypeRef {
    CppContext *context = nullptr;
};

class Namespace {

};

class RecordField {
};

class Record {
    std::vector<RecordField> members;
};

class ObjCClass {
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CPPNODES_H

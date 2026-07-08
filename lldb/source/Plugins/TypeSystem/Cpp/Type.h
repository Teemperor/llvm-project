//===-- CppContext.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H


namespace lldb_private {
namespace cpp_typesystem {

class Context;
class Record;

/// Represents everything needed to understand a type.
class Type {
public:
  // TODO: Virtual functions for the queries needed by TypeSystemCpp.
  // E.g., GetSize().
private:
  // If this is a class type, then this goes here.
  Record *m_record;
};

class RecordType : public Type {
public:
  // Overwrite the Type functions and forward to queries on m_record;
private:
  // If this is a class type, then this goes here.
  Record *m_record;
};


/// A simple pointer type.
class PointerType : public Type {
public:
  // Overwrite the Type functions and forward to queries on m_record;
private:
  // The base type of this pointer.
  // E.g., for `int *` this is a ref to `int.
  TypeRef m_base_type;
};

/// References a type, potentially in another Context.
class TypeRef {
public:
private:
  Context *m_context = nullptr;
  Type *m_type = nullptr;
};

static_assert(sizeof(TypeRef) <= sizeof(void *) * 2, "TypeRef is expected to be a small reference class!");


}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H

//===-- ObjCMethodSignature.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjCMethodSignature.h"

#include "Builder.h"
#include "Type.h"
#include "TypeObjC.h"
#include "TypeSystemCpp.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

CompilerType cpp_typesystem::CreateOpaqueObjCRecordType(Builder &builder,
                                                        llvm::StringRef name) {
  CompilerType record = builder.CreateRecordType(
      name, /*byte_size=*/1, /*is_cpp_class=*/false, /*is_union=*/false);
  if (auto *rt = llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(
          TypeSystemCpp::GetCppType(record.GetOpaqueQualType())))
    builder.SetRecordComplete(*rt);
  return record;
}

CompilerType cpp_typesystem::RealizeObjCEncoding(Builder &builder,
                                                 llvm::StringRef &enc) {
  // Skip leading method/ivar qualifier characters (const, in/out, byref, ...).
  while (!enc.empty() && llvm::StringRef("rnNoRVA").contains(enc.front()))
    enc = enc.drop_front();
  if (enc.empty())
    return CompilerType();
  const char c = enc.front();
  enc = enc.drop_front();
  auto builtin = [&](const char *name, uint64_t size, lldb::Encoding e,
                     lldb::Format f) {
    return builder.GetBuiltinType(name, size, e, f);
  };
  switch (c) {
  case 'c':
    return builtin("char", 1, lldb::eEncodingSint, lldb::eFormatChar);
  case 'C':
    return builtin("unsigned char", 1, lldb::eEncodingUint, lldb::eFormatChar);
  case 'B':
    return builtin("bool", 1, lldb::eEncodingUint, lldb::eFormatBoolean);
  case 's':
    return builtin("short", 2, lldb::eEncodingSint, lldb::eFormatDecimal);
  case 'S':
    return builtin("unsigned short", 2, lldb::eEncodingUint,
                   lldb::eFormatUnsigned);
  case 'i':
    return builtin("int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  case 'I':
    return builtin("unsigned int", 4, lldb::eEncodingUint,
                   lldb::eFormatUnsigned);
  case 'l':
    return builtin("long", 8, lldb::eEncodingSint, lldb::eFormatDecimal);
  case 'L':
    return builtin("unsigned long", 8, lldb::eEncodingUint,
                   lldb::eFormatUnsigned);
  case 'q':
    return builtin("long long", 8, lldb::eEncodingSint, lldb::eFormatDecimal);
  case 'Q':
    return builtin("unsigned long long", 8, lldb::eEncodingUint,
                   lldb::eFormatUnsigned);
  case 'f':
    return builtin("float", 4, lldb::eEncodingIEEE754, lldb::eFormatFloat);
  case 'd':
    return builtin("double", 8, lldb::eEncodingIEEE754, lldb::eFormatFloat);
  case 'v':
    return builder.GetVoidType();
  case '*': // char *
    return builder.CreatePointerType(
        builtin("char", 1, lldb::eEncodingSint, lldb::eFormatChar));
  case '@': // id
    // Named (not just an anonymous pointer) so ClangASTGenerator::GenerateType
    // recognizes it by its typedef name and maps it to clang's builtin `id`
    // (see the comment there): a method whose return/parameter type is `id`
    // needs real ObjC `id` semantics (e.g. implicit conversions to/from any
    // object pointer) for Sema to accept it, not just an opaque `void *`.
    // The pointee is the opaque `objc_object` record (not a null/empty
    // pointee) so that the ObjC language runtime recognizes a value of this
    // type as a possible dynamic type (see IsPossibleDynamicType) and resolves
    // its real class -- e.g. NSException's `id`-typed ivars showing their
    // NSString/NSDictionary summaries.
    return builder.CreateTypedefType(
        "id", builder.CreatePointerType(
                  CreateOpaqueObjCRecordType(builder, "objc_object")));
  case '#': // Class
    return builder.CreateTypedefType(
        "Class", builder.CreatePointerType(
                     CreateOpaqueObjCRecordType(builder, "objc_class")));
  case ':': // SEL
    return builder.CreateTypedefType(
        "SEL", builder.CreatePointerType(
                   CreateOpaqueObjCRecordType(builder, "objc_selector")));
  case '^': { // pointer to the following encoding
    CompilerType pointee = RealizeObjCEncoding(builder, enc);
    return builder.CreatePointerType(pointee);
  }
  default:
    return CompilerType();
  }
}

ObjCRuntimeMethodSignature::ObjCRuntimeMethodSignature(const char *types) {
  if (!types)
    return;
  const char *cursor = types;
  enum State { Start, InType, InPos } state = Start;
  const char *type_start = nullptr;
  int brace_depth = 0;
  uint32_t steps_left = 256;

  while (true) {
    if (--steps_left == 0)
      return;
    switch (state) {
    case Start:
      if (*cursor == '\0') {
        m_is_valid = true;
        return;
      }
      if (llvm::isDigit(*cursor))
        return; // A type-encoding can't start with a digit.
      state = InType;
      type_start = cursor;
      break;
    case InType:
      switch (*cursor) {
      case '\0':
        return; // A type must be followed by its stack-offset digits.
      case '[':
      case '{':
      case '(':
        ++brace_depth;
        ++cursor;
        break;
      case ']':
      case '}':
      case ')':
        if (!brace_depth)
          return;
        --brace_depth;
        ++cursor;
        break;
      default:
        if (llvm::isDigit(*cursor) && !brace_depth) {
          m_types.push_back(std::string(type_start, cursor - type_start));
          type_start = nullptr;
          state = InPos;
        } else {
          ++cursor;
        }
        break;
      }
      break;
    case InPos:
      if (*cursor == '\0') {
        m_is_valid = true;
        return;
      }
      if (llvm::isDigit(*cursor)) {
        ++cursor;
      } else {
        state = InType;
        type_start = cursor;
      }
      break;
    }
  }
}

void cpp_typesystem::AddRuntimeObjCMethod(Builder &builder,
                                          ObjCInterfaceType &iface,
                                          llvm::StringRef class_name,
                                          const char *selector,
                                          const char *type_encoding,
                                          bool is_class_method) {
  if (!selector || !selector[0])
    return;
  ObjCRuntimeMethodSignature sig(type_encoding);
  // A method's encoding is at least [return, self, _cmd]; reject anything
  // shorter as corrupt runtime metadata.
  if (!sig || sig.GetNumTypes() < 3)
    return;

  llvm::StringRef return_enc = sig.GetTypeAtIndex(0);
  CompilerType return_type = RealizeObjCEncoding(builder, return_enc);
  // A return/parameter type RealizeObjCEncoding can't decode (e.g. a
  // struct-by-value return/argument) means the method can't be fully typed;
  // drop it rather than synthesize a signature Sema would type-check
  // incorrectly. Mirrors AppleObjCDeclVendor::ObjCRuntimeMethodType::
  // BuildMethod, which likewise gives up on such a method instead of
  // approximating it.
  if (!return_type)
    return;

  // Indices 1 and 2 are the implicit self/_cmd parameters, which
  // cpp_typesystem::ObjCMethod's FunctionType does not carry (matching the
  // DWARF path -- see DWARFASTParserCpp::CompleteObjCMethodsFromDWARF).
  CompilerType func_type = builder.CreateFunctionType(
      return_type, /*is_variadic=*/false,
      /*use_void_for_empty_params=*/sig.GetNumTypes() == 3);
  for (size_t i = 3; i < sig.GetNumTypes(); ++i) {
    llvm::StringRef param_enc = sig.GetTypeAtIndex(i);
    CompilerType param_type = RealizeObjCEncoding(builder, param_enc);
    if (!param_type)
      return;
    builder.AddParameter(func_type, param_type);
  }

  std::string full_name = (llvm::Twine(is_class_method ? "+[" : "-[") +
                           class_name + " " + selector + "]")
                              .str();
  builder.AddObjCMethod(iface, full_name, func_type, /*asm_label=*/"",
                        is_class_method, /*is_variadic=*/false);
}

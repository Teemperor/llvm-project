//===-- ObjCMethodSignatureTest.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/ObjCMethodSignature.h"
#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"
#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeC.h"
#include "Plugins/TypeSystem/Clike/TypeObjC.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

namespace {
struct ObjCMethodSignatureTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::cantFail(clike_typesystem::LanguageOpts::Create(
                  llvm::Triple("arm64-apple-macosx"))));
  Builder builder{*ts};

  /// The single type an encoding realizes to, or an empty CompilerType.
  CompilerType Realize(llvm::StringRef encoding) {
    llvm::StringRef enc = encoding;
    return RealizeObjCEncoding(builder, enc);
  }

  std::string NameOf(llvm::StringRef encoding) {
    CompilerType type = Realize(encoding);
    if (!type)
      return "<invalid>";
    return type.GetTypeName().GetString();
  }
};
} // namespace

// A method encoding splits into return type plus one entry per argument, with
// the stack-offset digits that follow each type discarded.
TEST_F(ObjCMethodSignatureTest, SplitsEncodingDiscardingOffsets) {
  // -[Foo intValue]: returns int, implicit self (@) and _cmd (:).
  ObjCRuntimeMethodSignature sig("i16@0:8");
  ASSERT_TRUE(static_cast<bool>(sig));
  ASSERT_EQ(sig.GetNumTypes(), 3u);
  EXPECT_EQ(sig.GetTypeAtIndex(0), "i");
  EXPECT_EQ(sig.GetTypeAtIndex(1), "@");
  EXPECT_EQ(sig.GetTypeAtIndex(2), ":");
}

// An explicit argument shows up after the two implicit ones.
TEST_F(ObjCMethodSignatureTest, ExplicitArguments) {
  // -[Foo setName:(id)]: void return, self, _cmd, then an object argument.
  ObjCRuntimeMethodSignature sig("v24@0:8@16");
  ASSERT_TRUE(static_cast<bool>(sig));
  ASSERT_EQ(sig.GetNumTypes(), 4u);
  EXPECT_EQ(sig.GetTypeAtIndex(0), "v");
  EXPECT_EQ(sig.GetTypeAtIndex(3), "@");
}

// A digit inside a {...} / [...] / (...) group is part of the type -- an array
// or bitfield count -- not the argument's stack offset, so brace depth has to
// be tracked to tell them apart.
TEST_F(ObjCMethodSignatureTest, DigitsInsideGroupsAreNotOffsets) {
  // A struct-by-value argument whose encoding contains digits.
  ObjCRuntimeMethodSignature struct_sig("v24@0:8{CGPoint=dd}16");
  ASSERT_TRUE(static_cast<bool>(struct_sig));
  ASSERT_EQ(struct_sig.GetNumTypes(), 4u);
  EXPECT_EQ(struct_sig.GetTypeAtIndex(3), "{CGPoint=dd}");

  // An array argument: the 4 is the element count, part of the type.
  ObjCRuntimeMethodSignature array_sig("v24@0:8[4i]16");
  ASSERT_TRUE(static_cast<bool>(array_sig));
  ASSERT_EQ(array_sig.GetNumTypes(), 4u);
  EXPECT_EQ(array_sig.GetTypeAtIndex(3), "[4i]");
}

// An empty encoding yields nothing to work with.
TEST_F(ObjCMethodSignatureTest, EmptyEncoding) {
  ObjCRuntimeMethodSignature sig("");
  EXPECT_EQ(sig.GetNumTypes(), 0u);
}

// An unterminated group is corrupt runtime metadata and must be reported as
// invalid rather than yielding a half-parsed type list.
TEST_F(ObjCMethodSignatureTest, UnterminatedGroupIsInvalid) {
  ObjCRuntimeMethodSignature sig("v24@0:8{CGPoint=dd16");
  EXPECT_FALSE(static_cast<bool>(sig));
}

// The scalar encodings realize to the corresponding builtin types.
TEST_F(ObjCMethodSignatureTest, RealizesScalarEncodings) {
  EXPECT_EQ(NameOf("v"), "void");
  EXPECT_EQ(NameOf("c"), "char");
  EXPECT_EQ(NameOf("i"), "int");
  EXPECT_EQ(NameOf("s"), "short");
  EXPECT_EQ(NameOf("l"), "long");
  EXPECT_EQ(NameOf("f"), "float");
  EXPECT_EQ(NameOf("d"), "double");
  EXPECT_EQ(NameOf("B"), "bool");
}

// The unsigned encodings are the capitalised forms of the signed ones.
TEST_F(ObjCMethodSignatureTest, RealizesUnsignedEncodings) {
  EXPECT_EQ(NameOf("C"), "unsigned char");
  EXPECT_EQ(NameOf("I"), "unsigned int");
  EXPECT_EQ(NameOf("S"), "unsigned short");
}

// A `^` prefix is a pointer to whatever follows it.
TEST_F(ObjCMethodSignatureTest, RealizesPointerEncodings) {
  CompilerType void_ptr = Realize("^v");
  ASSERT_TRUE(static_cast<bool>(void_ptr));
  EXPECT_TRUE(void_ptr.IsPointerType());

  CompilerType int_ptr = Realize("^i");
  ASSERT_TRUE(static_cast<bool>(int_ptr));
  CompilerType pointee;
  ASSERT_TRUE(int_ptr.IsPointerType(&pointee));
  EXPECT_EQ(pointee.GetTypeName().GetString(), "int");
}

// The object encodings realize to the pointer types `id`/`Class`/`SEL` are
// modeled as.
TEST_F(ObjCMethodSignatureTest, RealizesObjectEncodings) {
  for (const char *enc : {"@", "#", ":"}) {
    CompilerType type = Realize(enc);
    ASSERT_TRUE(static_cast<bool>(type)) << "encoding " << enc;
    EXPECT_TRUE(type.IsPointerType()) << "encoding " << enc;
  }
}

// Realizing consumes exactly the encoding it used, so a caller can walk a
// multi-type string.
TEST_F(ObjCMethodSignatureTest, RealizeAdvancesPastWhatItConsumed) {
  llvm::StringRef enc = "i^vd";
  CompilerType first = RealizeObjCEncoding(builder, enc);
  ASSERT_TRUE(static_cast<bool>(first));
  EXPECT_EQ(first.GetTypeName().GetString(), "int");
  EXPECT_EQ(enc, "^vd");

  CompilerType second = RealizeObjCEncoding(builder, enc);
  ASSERT_TRUE(static_cast<bool>(second));
  EXPECT_TRUE(second.IsPointerType());
  EXPECT_EQ(enc, "d");

  CompilerType third = RealizeObjCEncoding(builder, enc);
  ASSERT_TRUE(static_cast<bool>(third));
  EXPECT_EQ(third.GetTypeName().GetString(), "double");
  EXPECT_TRUE(enc.empty());
}

// A struct-by-value encoding is not handled, and reports so rather than
// approximating the type.
TEST_F(ObjCMethodSignatureTest, StructByValueIsUnsupported) {
  EXPECT_FALSE(static_cast<bool>(Realize("{CGPoint=dd}")));
  // ...as is an outright unknown encoding character.
  EXPECT_FALSE(static_cast<bool>(Realize("\x01")));
  llvm::StringRef empty;
  EXPECT_FALSE(static_cast<bool>(RealizeObjCEncoding(builder, empty)));
}

// The opaque runtime records `id`/`Class`/`SEL` point at are created complete
// and empty, so a value of one can be dereferenced instead of erroring out on
// an incomplete type.
TEST_F(ObjCMethodSignatureTest, OpaqueRecordIsCompleteAndEmpty) {
  CompilerType record = CreateOpaqueObjCRecordType(builder, "objc_object");
  ASSERT_TRUE(static_cast<bool>(record));
  EXPECT_EQ(record.GetTypeName().GetString(), "objc_object");
  EXPECT_TRUE(record.IsAggregateType());
  EXPECT_TRUE(ts->GetCompleteType(record.GetOpaqueQualType()));
  // One byte: the minimum for a complete record, so a deref child exists.
  llvm::Expected<uint64_t> size = record.GetByteSize(nullptr);
  ASSERT_TRUE(static_cast<bool>(size));
  EXPECT_EQ(*size, 1u);
}

// A runtime-reported selector becomes an ObjCMethod on the interface, with the
// implicit self/_cmd parameters stripped from its signature.
TEST_F(ObjCMethodSignatureTest, AddsRuntimeMethod) {
  CompilerType iface_type = builder.CreateObjCInterfaceType("Foo", 8);
  auto *iface = llvm::cast<ObjCInterfaceType>(
      TypeSystemClike::GetClikeType(iface_type.GetOpaqueQualType()));

  AddRuntimeObjCMethod(builder, *iface, "Foo", "intValue", "i16@0:8",
                       /*is_class_method=*/false);
  ASSERT_EQ(iface->GetNumObjCMethods(), 1u);
  const ObjCMethod *method = iface->GetObjCMethodAtIndex(0);
  ASSERT_NE(method, nullptr);
  EXPECT_EQ(method->name.GetName(), "-[Foo intValue]");
  EXPECT_FALSE(method->is_class_method);

  // self/_cmd are implicit and excluded from the stored signature.
  auto *fn = llvm::cast<FunctionType>(Desugar(&method->type.Get()));
  EXPECT_EQ(fn->GetNumParameters(), 0u);
  EXPECT_EQ(fn->GetReturnType()->GetName().GetName(), "int");
}

// A class method is spelled with a leading `+`.
TEST_F(ObjCMethodSignatureTest, AddsClassMethod) {
  CompilerType iface_type = builder.CreateObjCInterfaceType("Foo", 8);
  auto *iface = llvm::cast<ObjCInterfaceType>(
      TypeSystemClike::GetClikeType(iface_type.GetOpaqueQualType()));

  AddRuntimeObjCMethod(builder, *iface, "Foo", "alloc", "@16@0:8",
                       /*is_class_method=*/true);
  ASSERT_EQ(iface->GetNumObjCMethods(), 1u);
  const ObjCMethod *method = iface->GetObjCMethodAtIndex(0);
  EXPECT_EQ(method->name.GetName(), "+[Foo alloc]");
  EXPECT_TRUE(method->is_class_method);
}

// A method whose encoding can't be fully decoded is dropped rather than being
// added with an approximated signature.
TEST_F(ObjCMethodSignatureTest, DropsUndecodableMethod) {
  CompilerType iface_type = builder.CreateObjCInterfaceType("Foo", 8);
  auto *iface = llvm::cast<ObjCInterfaceType>(
      TypeSystemClike::GetClikeType(iface_type.GetOpaqueQualType()));

  // A struct-by-value argument is not realizable.
  AddRuntimeObjCMethod(builder, *iface, "Foo", "takesPoint:",
                       "v24@0:8{CGPoint=dd}16", /*is_class_method=*/false);
  EXPECT_EQ(iface->GetNumObjCMethods(), 0u);

  // So is corrupt metadata.
  AddRuntimeObjCMethod(builder, *iface, "Foo", "broken", "v24@0:8{oops",
                       /*is_class_method=*/false);
  EXPECT_EQ(iface->GetNumObjCMethods(), 0u);
}

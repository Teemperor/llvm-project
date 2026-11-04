//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/TypeC.h"
#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"
#include "Plugins/TypeSystem/Clike/Type.h"

#include "llvm/ADT/APInt.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

namespace {
struct TypeCTest : public testing::Test {
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};

  Type *Int() { return context.GetBuiltinType(BuiltinKind::Int); }
  Type *Void() { return context.GetBuiltinType(BuiltinKind::Void); }
};
} // namespace

// A pointer takes the target's pointer width from the Context that owns it,
// rather than storing a size of its own -- so even `void *` has a size.
TEST_F(TypeCTest, PointerSizeComesFromContext) {
  PointerType *ptr = context.CreatePointerType(TypeRef(*Int()));
  EXPECT_EQ(ptr->GetPointeeType(), Int());
  EXPECT_TRUE(ptr->IsPointer());
  EXPECT_FALSE(ptr->IsBlockPointer());
  EXPECT_EQ(ptr->GetByteSize(),
            std::optional<uint64_t>(context.GetPointerSize()));
  EXPECT_EQ(ptr->GetByteSize(), std::optional<uint64_t>(8));
  // Pointer-aligned, i.e. its own size.
  EXPECT_EQ(ptr->GetAlignmentInBits(), std::optional<uint64_t>(64));

  PointerType *void_ptr = context.CreatePointerType(TypeRef(*Void()));
  EXPECT_EQ(void_ptr->GetByteSize(), std::optional<uint64_t>(8));
}

// A pointer on a 32-bit target is 4 bytes: the width really does follow the
// Context's target rather than being hardcoded.
TEST_F(TypeCTest, PointerSizeFollowsTarget) {
  Context context32{
      llvm::cantFail(LanguageOpts::Create(llvm::Triple("i386-pc-linux-gnu")))};
  PointerType *ptr = context32.CreatePointerType(
      TypeRef(*context32.GetBuiltinType(BuiltinKind::Int)));
  EXPECT_EQ(ptr->GetByteSize(), std::optional<uint64_t>(4));
  EXPECT_EQ(ptr->GetAlignmentInBits(), std::optional<uint64_t>(32));
}

// A typedef is sugar: it reports its own name and type class but forwards
// layout to what it aliases, and desugars through to the canonical type.
TEST_F(TypeCTest, Typedef) {
  TypedefType *alias = context.CreateTypedefType("my_int", TypeRef(*Int()));
  EXPECT_EQ(alias->GetName().GetName(), "my_int");
  EXPECT_EQ(alias->GetUnderlyingType(), Int());
  EXPECT_EQ(alias->GetByteSize(), Int()->GetByteSize());
  EXPECT_EQ(alias->GetTypeClass(), lldb::eTypeClassTypedef);
  EXPECT_EQ(alias->Desugar(), Int());
  EXPECT_EQ(alias->GetTypedefedType(), Int());
  EXPECT_TRUE(llvm::isa<SugarType>(static_cast<Type *>(alias)));
}

// A cv-qualified type is sugar that records which qualifiers apply, and the
// CVR mask collapses a whole chain of them.
TEST_F(TypeCTest, CVQualified) {
  CVQualifiedType *c =
      context.CreateCVQualifiedType(TypeRef(*Int()), /*is_const=*/true,
                                    /*is_volatile=*/false);
  EXPECT_TRUE(c->IsConst());
  EXPECT_FALSE(c->IsVolatile());
  EXPECT_EQ(c->Desugar(), Int());
  EXPECT_EQ(c->GetByteSize(), Int()->GetByteSize());
  EXPECT_EQ(c->GetCVRQualifiers(), 0x1u);

  // `const volatile int` through a chain of two qualifiers.
  CVQualifiedType *cv =
      context.CreateCVQualifiedType(TypeRef(*c), /*is_const=*/false,
                                    /*is_volatile=*/true);
  EXPECT_EQ(cv->GetCVRQualifiers(), 0x1u | 0x4u);
  EXPECT_EQ(cv->Desugar(), Int());
  EXPECT_EQ(cv->StripCVQualifiers(), Int());
  // IsConst only looks at the outermost qualifier.
  EXPECT_FALSE(cv->IsConst());

  // A non-qualified type carries no qualifiers.
  EXPECT_EQ(Int()->GetCVRQualifiers(), 0u);

  // Only cv-sugar at the top counts: a typedef stops both the mask and the
  // stripping.
  TypedefType *alias = context.CreateTypedefType("cint", TypeRef(*c));
  EXPECT_EQ(alias->GetCVRQualifiers(), 0u);
  CVQualifiedType *v_alias =
      context.CreateCVQualifiedType(TypeRef(*alias), /*is_const=*/false,
                                    /*is_volatile=*/true);
  EXPECT_EQ(v_alias->GetCVRQualifiers(), 0x4u);
  EXPECT_EQ(v_alias->StripCVQualifiers(), alias);
}

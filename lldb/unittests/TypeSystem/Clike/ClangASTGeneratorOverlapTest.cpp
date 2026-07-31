//===-- ClangASTGeneratorOverlapTest.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Clang's record lowering requires the members it is handed to have
// non-overlapping storage (CGRecordLowering::checkBitfieldClipping asserts on
// it). DWARF-derived layouts can violate that in two ways, both covered here:
// a `[[no_unique_address]]` member whose tail padding the next member reuses
// (valid, and pervasive in libc++'s `__compressed_pair`), and plainly
// inconsistent debug info where a member's type is too big for its slot.
//
//===----------------------------------------------------------------------===//

#include "ClangASTGeneratorTestUtils.h"

#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeC.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"

#include "llvm/Support/Casting.h"

#include <string>
#include <vector>

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

namespace {
class ClangASTGeneratorOverlapTest : public ClangASTGeneratorTestUtils {
protected:
  static clike_typesystem::Type *Raw(CompilerType type) {
    return static_cast<clike_typesystem::Type *>(type.GetOpaqueQualType());
  }

  RecordType &MakeRecord(llvm::StringRef name, uint64_t byte_size) {
    return *llvm::cast<RecordType>(
        Raw(builder.CreateRecordType(name, byte_size, /*is_cpp_class=*/true)));
  }

  void AddField(RecordType &record, llvm::StringRef name, CompilerType type,
                uint64_t byte_offset) {
    builder.AddField(record, builder.GetIdentifier(name), Raw(type),
                     byte_offset);
  }

  CompilerType GetLong() {
    return builder.GetBuiltinType("long", 8, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }
  CompilerType GetBool() {
    return builder.GetBuiltinType("bool", 1, lldb::eEncodingUint,
                                  lldb::eFormatBoolean);
  }

  /// Complete \p record's clang decl and return the names of the fields it got.
  std::vector<std::string> GenerateFieldNames(ClangASTGenerator &generator,
                                              RecordType &record,
                                              clang::CXXRecordDecl **out_decl) {
    clang::QualType qt = generator.Generate(ts->GetCompilerType(&record));
    clang::TagDecl *tag = qt->getAsTagDecl();
    EXPECT_NE(tag, nullptr);
    EXPECT_TRUE(generator.CompleteRecord(tag));
    auto *cxx = llvm::cast<clang::CXXRecordDecl>(tag);
    if (out_decl)
      *out_decl = cxx;
    std::vector<std::string> names;
    for (const clang::FieldDecl *fd : cxx->fields())
      names.push_back(fd->getNameAsString());
    return names;
  }
};
} // namespace

// A member that starts inside the previous member's tail padding is what
// `[[no_unique_address]]` looks like once the attribute is gone (DWARF cannot
// spell it). Both members must survive, and the earlier one must be marked
// potentially-overlapping so Clang gives it only its dsize worth of storage.
TEST_F(ClangASTGeneratorOverlapTest, TailPaddingReuseInfersNoUniqueAddress) {
  // struct Del { long &r; bool b; };  // sizeof 16, dsize 9
  RecordType &del = MakeRecord("Del", 16);
  // A reference member keeps `Del` from being POD, which is what allows its
  // tail padding to be reused at all.
  AddField(del, "r", builder.CreateReferenceType(GetLong(), /*is_rvalue=*/false),
           0);
  AddField(del, "b", GetBool(), 8);
  builder.SetRecordComplete(del);

  // struct Pad { char p[7]; };
  RecordType &pad = MakeRecord("Pad", 7);
  AddField(pad, "p",
           builder.CreateArrayType(
               builder.GetBuiltinType("char", 1, lldb::eEncodingSint,
                                      lldb::eFormatChar),
               7),
           0);
  builder.SetRecordComplete(pad);

  // struct Structure {
  //   long *ptr;
  //   [[no_unique_address]] Del del;   // at 8, sizeof 16 -> would end at 24
  //   [[no_unique_address]] Pad pad;   // at 17, inside del's tail padding
  // };
  RecordType &structure = MakeRecord("Structure", 24);
  AddField(structure, "ptr", builder.CreatePointerType(GetLong()), 0);
  AddField(structure, "del", ts->GetCompilerType(&del), 8);
  AddField(structure, "pad", ts->GetCompilerType(&pad), 17);
  builder.SetRecordComplete(structure);

  ClangASTGenerator generator(ast);
  clang::CXXRecordDecl *decl = nullptr;
  EXPECT_EQ(GenerateFieldNames(generator, structure, &decl),
            std::vector<std::string>({"ptr", "del", "pad"}));

  auto field = decl->field_begin();
  EXPECT_FALSE((*field)->hasAttr<clang::NoUniqueAddressAttr>()); // ptr
  ++field;
  EXPECT_TRUE((*field)->hasAttr<clang::NoUniqueAddressAttr>()); // del
  EXPECT_TRUE((*field)->isPotentiallyOverlapping());
}

// An empty member legitimately shares another member's offset; Clang gives it
// no storage at all, so it must not be treated as an overlap (nor dropped).
TEST_F(ClangASTGeneratorOverlapTest, EmptyMemberAtSharedOffsetIsKept) {
  RecordType &empty = MakeRecord("Empty", 1);
  builder.SetRecordComplete(empty);

  RecordType &structure = MakeRecord("Structure", 8);
  AddField(structure, "ptr", builder.CreatePointerType(GetLong()), 0);
  AddField(structure, "e", ts->GetCompilerType(&empty), 0);
  builder.SetRecordComplete(structure);

  ClangASTGenerator generator(ast);
  clang::CXXRecordDecl *decl = nullptr;
  EXPECT_EQ(GenerateFieldNames(generator, structure, &decl),
            std::vector<std::string>({"ptr", "e"}));
  for (const clang::FieldDecl *fd : decl->fields())
    EXPECT_FALSE(fd->hasAttr<clang::NoUniqueAddressAttr>());
}

// Inconsistent debug info: `big` sits at offset 0 with a 16-byte type inside an
// 8-byte record that already has an 8-byte member there (seen with
// dsymutil-deduplicated DWARF, where a member's type reference can resolve to
// an unrelated same-named type). Nothing can be salvaged, so the member is
// dropped rather than handed to Clang as an overlapping one.
TEST_F(ClangASTGeneratorOverlapTest, ImpossibleOverlapDropsMember) {
  RecordType &big = MakeRecord("Big", 16);
  AddField(big, "a", GetLong(), 0);
  AddField(big, "b", GetLong(), 8);
  builder.SetRecordComplete(big);

  RecordType &bad = MakeRecord("Bad", 8);
  AddField(bad, "ptr", builder.CreatePointerType(GetLong()), 0);
  AddField(bad, "big", ts->GetCompilerType(&big), 0);
  builder.SetRecordComplete(bad);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateFieldNames(generator, bad, nullptr),
            std::vector<std::string>({"ptr"}));
}

// A union's members all share offset 0 by design (and Clang lowers them through
// a path with no such requirement), so none of them may be dropped.
TEST_F(ClangASTGeneratorOverlapTest, UnionMembersAreNotDropped) {
  CompilerType union_ct = builder.CreateRecordType("U", 8, /*is_cpp_class=*/true,
                                                   /*is_union=*/true);
  auto &u = *llvm::cast<RecordType>(Raw(union_ct));
  AddField(u, "l", GetLong(), 0);
  AddField(u, "b", GetBool(), 0);
  AddField(u, "p", builder.CreatePointerType(GetLong()), 0);
  builder.SetRecordComplete(u);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateFieldNames(generator, u, nullptr),
            std::vector<std::string>({"l", "b", "p"}));
}

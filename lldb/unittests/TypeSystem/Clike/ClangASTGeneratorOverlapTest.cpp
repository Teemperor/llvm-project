//===-- ClangASTGeneratorOverlapTest.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Clang's record lowering requires the members it is handed to have
// non-overlapping storage that fits inside the record
// (CGRecordLowering::checkBitfieldClipping / ::insertPadding assert on it).
// DWARF-derived layouts can violate that in several ways, all covered here: a
// `[[no_unique_address]]` member whose tail padding the next member reuses
// (valid, and pervasive in libc++'s `__compressed_pair`), plainly inconsistent
// debug info where a member's type is too big for its slot, and the same for a
// *base class* subobject. A bitfield is no exception: Clang decides how wide
// the access unit wrapping a bitfield run is, but that unit still starts at the
// byte holding the run's first bit, so a bitfield has to clear everything laid
// out before it just like a plain field's storage does.
//
//===----------------------------------------------------------------------===//

#include "ClangASTGeneratorTestUtils.h"

#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeC.h"
#include "Plugins/TypeSystem/Clike/TypeCpp.h"

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

  /// Add a bitfield \p name of \p bit_size bits whose *absolute* bit offset in
  /// \p record is \p bit_offset, described the way the DWARF parser does it: a
  /// storage-unit byte offset plus the bit offset within that unit.
  void AddBitfield(RecordType &record, llvm::StringRef name, CompilerType type,
                   uint64_t bit_offset, uint32_t bit_size) {
    uint64_t storage_bits = *Raw(type)->GetByteSize() * 8;
    uint32_t bit_in_unit = bit_offset % storage_bits;
    builder.AddField(record, builder.GetIdentifier(name), Raw(type),
                     (bit_offset - bit_in_unit) / 8, bit_size, bit_in_unit);
  }

  void AddBase(RecordType &record, CompilerType type, uint64_t byte_offset) {
    builder.AddBaseClass(llvm::cast<ClassType>(record), Raw(type), byte_offset);
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

  /// Complete \p record's clang decl and return the names of the base classes it
  /// got.
  std::vector<std::string> GenerateBaseNames(ClangASTGenerator &generator,
                                             RecordType &record) {
    clang::CXXRecordDecl *decl = nullptr;
    GenerateFieldNames(generator, record, &decl);
    std::vector<std::string> names;
    for (const clang::CXXBaseSpecifier &base : decl->bases())
      if (auto *base_rd = base.getType()->getAsCXXRecordDecl())
        names.push_back(base_rd->getNameAsString());
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

// The overlap check has to size a member by the *clang* type the generator
// hands Clang, not by the LLDB type the enclosing record was laid out against.
// Those two disagree whenever two modules define a same-named record
// differently: the generator unifies records by fully-qualified name (so a type
// forward-declared in one module and defined in another stays a single clang
// type), which means the first module to be generated owns the clang decl and
// everybody else's LLDB byte size for that name is irrelevant to how much
// storage Clang gives a member of it.
//
// Here "BBox" is 24 bytes in the module generated first and 16 bytes in the one
// `Cellule` comes from, so `hash` at byte 16 looks fine against the 16-byte
// LLDB size but sits inside the 24 bytes Clang gives `box`.
TEST_F(ClangASTGeneratorOverlapTest, MemberSizeComesFromTheClangType) {
  // The "other module": struct BBox { long a, b; char c; }; -- 24 bytes.
  auto other_ts = std::make_shared<TypeSystemClike>(
      "other", llvm::Triple("x86_64-pc-linux-gnu"));
  clike_typesystem::Builder other_builder{*other_ts};
  auto &other_bbox = *llvm::cast<RecordType>(
      static_cast<clike_typesystem::Type *>(
          other_builder.CreateRecordType("BBox", 24, /*is_cpp_class=*/true)
              .GetOpaqueQualType()));
  // The other module's own `long`: a Context's builtins belong to it like any
  // other type, so this record must not reference this fixture's ones.
  CompilerType other_long = other_builder.GetBuiltinType(
      "long", 8, lldb::eEncodingSint, lldb::eFormatDecimal);
  other_builder.AddField(other_bbox, other_builder.GetIdentifier("a"),
                         Raw(other_long), 0);
  other_builder.AddField(other_bbox, other_builder.GetIdentifier("b"),
                         Raw(other_long), 8);
  other_builder.AddField(
      other_bbox, other_builder.GetIdentifier("c"),
      Raw(other_builder.GetBuiltinType("char", 1, lldb::eEncodingSint,
                                       lldb::eFormatChar)),
      16);
  other_builder.SetRecordComplete(other_bbox);

  // This module's own, smaller "BBox": struct BBox { long a, b; }; -- 16 bytes.
  RecordType &bbox = MakeRecord("BBox", 16);
  AddField(bbox, "a", GetLong(), 0);
  AddField(bbox, "b", GetLong(), 8);
  builder.SetRecordComplete(bbox);

  RecordType &cellule = MakeRecord("Cellule", 32);
  AddField(cellule, "box", ts->GetCompilerType(&bbox), 0);
  AddField(cellule, "hash", GetLong(), 16);
  AddField(cellule, "tail", GetLong(), 24);
  builder.SetRecordComplete(cellule);

  ClangASTGenerator generator(ast);
  // Generate the other module's BBox first, so it is the one that owns the
  // clang decl named "BBox" (this is the ordering the name unification makes
  // significant, and which module wins is not something the generator gets to
  // choose in a real target).
  clang::CXXRecordDecl *other_decl = nullptr;
  EXPECT_EQ(GenerateFieldNames(generator, other_bbox, &other_decl),
            std::vector<std::string>({"a", "b", "c"}));
  EXPECT_EQ(ast.getTypeSizeInChars(ast.getCanonicalTagType(other_decl))
                .getQuantity(),
            24);

  // `hash` cannot be laid out inside those 24 bytes, and nothing can be shrunk
  // to make room (BBox's dsize is 17 bytes, still past `hash`), so it goes.
  EXPECT_EQ(GenerateFieldNames(generator, cellule, nullptr),
            std::vector<std::string>({"box", "tail"}));
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

// A base subobject that runs past the end of the derived record cannot be laid
// out (`insertPadding` asserts on the capstone it places at the record's size).
// This is what dsymutil's ODR uniquing produces when a `DW_TAG_inheritance`
// resolves to a same-named nested type of an unrelated template
// specialization: here `Derived` is 16 bytes with `Big` (16 bytes) at offset 8.
// A base cannot be shrunk the way a `[[no_unique_address]]` field can, so it is
// dropped.
TEST_F(ClangASTGeneratorOverlapTest, OversizedBaseIsDropped) {
  RecordType &small = MakeRecord("Small", 8);
  AddField(small, "s", GetLong(), 0);
  builder.SetRecordComplete(small);

  RecordType &big = MakeRecord("Big", 16);
  AddField(big, "a", GetLong(), 0);
  AddField(big, "b", GetLong(), 8);
  builder.SetRecordComplete(big);

  RecordType &derived = MakeRecord("Derived", 16);
  AddBase(derived, ts->GetCompilerType(&small), 0);
  AddBase(derived, ts->GetCompilerType(&big), 8);
  builder.SetRecordComplete(derived);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateBaseNames(generator, derived),
            std::vector<std::string>({"Small"}));
}

// Two base subobjects at overlapping offsets: the later one (in offset order)
// has to go.
TEST_F(ClangASTGeneratorOverlapTest, OverlappingBaseIsDropped) {
  RecordType &big = MakeRecord("Big", 16);
  AddField(big, "a", GetLong(), 0);
  AddField(big, "b", GetLong(), 8);
  builder.SetRecordComplete(big);

  RecordType &small = MakeRecord("Small", 8);
  AddField(small, "s", GetLong(), 0);
  builder.SetRecordComplete(small);

  RecordType &derived = MakeRecord("Derived", 24);
  AddBase(derived, ts->GetCompilerType(&big), 0);
  AddBase(derived, ts->GetCompilerType(&small), 8);
  builder.SetRecordComplete(derived);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateBaseNames(generator, derived),
            std::vector<std::string>({"Big"}));
}

// An empty base gets no storage at all, so it legitimately shares the offset of
// the base (or field) after it and must be kept.
TEST_F(ClangASTGeneratorOverlapTest, EmptyBaseAtSharedOffsetIsKept) {
  RecordType &empty = MakeRecord("Empty", 1);
  builder.SetRecordComplete(empty);

  RecordType &payload = MakeRecord("Payload", 8);
  AddField(payload, "p", GetLong(), 0);
  builder.SetRecordComplete(payload);

  RecordType &derived = MakeRecord("Derived", 16);
  AddBase(derived, ts->GetCompilerType(&empty), 0);
  AddBase(derived, ts->GetCompilerType(&payload), 0);
  AddField(derived, "f", GetLong(), 8);
  builder.SetRecordComplete(derived);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateBaseNames(generator, derived),
            std::vector<std::string>({"Empty", "Payload"}));
}

// The bases are checked in *offset* order, which is not necessarily declaration
// order: Itanium lays the primary (polymorphic) base out first even when a
// non-polymorphic base is declared before it. Checking in declaration order
// would see `NonPoly` at offset 8 first and then wrongly drop `Poly` at 0.
TEST_F(ClangASTGeneratorOverlapTest, BasesOutOfDeclarationOrderAreKept) {
  RecordType &non_poly = MakeRecord("NonPoly", 8);
  AddField(non_poly, "n", GetLong(), 0);
  builder.SetRecordComplete(non_poly);

  RecordType &poly = MakeRecord("Poly", 8);
  AddField(poly, "p", GetLong(), 0);
  builder.SetRecordComplete(poly);

  RecordType &derived = MakeRecord("Derived", 16);
  AddBase(derived, ts->GetCompilerType(&non_poly), 8);
  AddBase(derived, ts->GetCompilerType(&poly), 0);
  builder.SetRecordComplete(derived);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateBaseNames(generator, derived),
            std::vector<std::string>({"NonPoly", "Poly"}));
}

// A field that starts inside a *base* subobject is as unlayoutable as one that
// starts inside a preceding field, and it cannot be fixed by marking anything
// potentially-overlapping (a base already only occupies its non-virtual size),
// so the field is dropped.
TEST_F(ClangASTGeneratorOverlapTest, FieldInsideBaseIsDropped) {
  RecordType &base = MakeRecord("Base", 16);
  AddField(base, "a", GetLong(), 0);
  AddField(base, "b", GetLong(), 8);
  builder.SetRecordComplete(base);

  RecordType &derived = MakeRecord("Derived", 24);
  AddBase(derived, ts->GetCompilerType(&base), 0);
  AddField(derived, "inside", GetLong(), 8);
  AddField(derived, "after", GetLong(), 16);
  builder.SetRecordComplete(derived);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateFieldNames(generator, derived, nullptr),
            std::vector<std::string>({"after"}));
}

// The same for a *bitfield* starting inside a base subobject. A bitfield has no
// storage of its own -- Clang decides how wide the access unit wrapping its run
// is -- but that unit still starts at the byte holding the bitfield's first bit,
// so it has to clear everything laid out before it just like a plain field's
// storage does (CGRecordLowering::checkBitfieldClipping, "Bitfield access unit
// is not clipped").
//
// This is what the fuzzer's deliberately ODR-inconsistent debug info produces:
// the derived class's definition comes from a translation unit where the base
// had fewer members (leaving the bitfield's byte free), while the base type
// resolves to a larger definition from another one.
TEST_F(ClangASTGeneratorOverlapTest, BitfieldInsideBaseIsDropped) {
  RecordType &base = MakeRecord("Base", 16);
  AddField(base, "a", GetLong(), 0);
  AddField(base, "b", GetLong(), 8);
  builder.SetRecordComplete(base);

  RecordType &derived = MakeRecord("Derived", 24);
  AddBase(derived, ts->GetCompilerType(&base), 0);
  // Bit 104 is byte 13, inside the 16-byte base subobject.
  AddBitfield(derived, "inside", GetInt(), 104, 19);
  AddField(derived, "after", GetLong(), 16);
  builder.SetRecordComplete(derived);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateFieldNames(generator, derived, nullptr),
            std::vector<std::string>({"after"}));
}

// A bitfield may legitimately sit in the tail padding of a preceding
// `[[no_unique_address]]` member, so -- exactly as for a plain field there
// (TailPaddingReuseInfersNoUniqueAddress above) -- the preceding member has to
// be re-described as potentially-overlapping rather than either member being
// dropped. Real, valid C++ this time, not inconsistent debug info:
//
//   struct Structure {
//     long *ptr;
//     [[no_unique_address]] Del del;  // at 8, sizeof 16 but dsize 9
//     unsigned bf : 3;                // at bit 136 (byte 17), in del's padding
//   };
TEST_F(ClangASTGeneratorOverlapTest, BitfieldInTailPaddingInfersNoUniqueAddress) {
  // struct Del { long &r; bool b; };  // sizeof 16, dsize 9
  RecordType &del = MakeRecord("Del", 16);
  AddField(del, "r", builder.CreateReferenceType(GetLong(), /*is_rvalue=*/false),
           0);
  AddField(del, "b", GetBool(), 8);
  builder.SetRecordComplete(del);

  RecordType &structure = MakeRecord("Structure", 24);
  AddField(structure, "ptr", builder.CreatePointerType(GetLong()), 0);
  AddField(structure, "del", ts->GetCompilerType(&del), 8);
  AddBitfield(structure, "bf", GetInt(), 136, 3);
  builder.SetRecordComplete(structure);

  ClangASTGenerator generator(ast);
  clang::CXXRecordDecl *decl = nullptr;
  EXPECT_EQ(GenerateFieldNames(generator, structure, &decl),
            std::vector<std::string>({"ptr", "del", "bf"}));

  auto field = decl->field_begin();
  EXPECT_FALSE((*field)->hasAttr<clang::NoUniqueAddressAttr>()); // ptr
  ++field;
  EXPECT_TRUE((*field)->hasAttr<clang::NoUniqueAddressAttr>()); // del
  EXPECT_TRUE((*field)->isPotentiallyOverlapping());
}

// Two bitfields whose bits overlap (again only possible with inconsistent debug
// info) land in overlapping access units, which Clang cannot lay out either.
// Neither can be shrunk, so the later one goes.
TEST_F(ClangASTGeneratorOverlapTest, OverlappingBitfieldIsDropped) {
  RecordType &structure = MakeRecord("Structure", 8);
  AddBitfield(structure, "wide", GetInt(), 0, 24);
  // Starts inside `wide`, at a byte boundary -- so it would begin a second
  // access unit overlapping the first rather than joining `wide`'s run.
  AddBitfield(structure, "inside", GetInt(), 8, 8);
  AddField(structure, "after", GetInt(), 4);
  builder.SetRecordComplete(structure);

  ClangASTGenerator generator(ast);
  EXPECT_EQ(GenerateFieldNames(generator, structure, nullptr),
            std::vector<std::string>({"wide", "after"}));
}

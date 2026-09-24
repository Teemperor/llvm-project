//===-- StructuredDataTest.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "TestingSupport/TestUtilities.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StructuredData.h"
#include "llvm/Support/Path.h"

using namespace lldb;
using namespace lldb_private;

TEST(StructuredDataTest, StringDump) {
  std::pair<llvm::StringRef, llvm::StringRef> TestCases[] = {
      {R"(asdfg)", R"("asdfg")"},
      {R"(as"df)", R"("as\"df")"},
      {R"(as\df)", R"("as\\df")"},
  };
  for (auto P : TestCases) {
    StreamString S;
    const bool pretty_print = false;
    StructuredData::String(P.first).Dump(S, pretty_print);
    EXPECT_EQ(P.second, S.GetString());
  }
}

TEST(StructuredDataTest, GetDescriptionEmpty) {
  Status status;
  auto object_sp = StructuredData::ParseJSON("{}");
  ASSERT_NE(nullptr, object_sp);

  StreamString S;
  object_sp->GetDescription(S);
  EXPECT_EQ(0u, S.GetSize());
}

TEST(StructuredDataTest, GetDescriptionBasic) {
  Status status;
  std::string input = GetInputFilePath("StructuredData-basic.json");
  auto object_sp = StructuredData::ParseJSONFromFile(FileSpec(input), status);
  ASSERT_NE(nullptr, object_sp);

  const std::string expected = "[0]: 1\n"
                               "[1]: 2\n"
                               "[2]: 3";

  StreamString S;
  object_sp->GetDescription(S);
  EXPECT_EQ(expected, S.GetString());
}

TEST(StructuredDataTest, GetDescriptionNested) {
  Status status;
  std::string input = GetInputFilePath("StructuredData-nested.json");
  auto object_sp = StructuredData::ParseJSONFromFile(FileSpec(input), status);
  ASSERT_NE(nullptr, object_sp);

  const std::string expected = "my_dict:\n"
                               "  [0]:\n"
                               "    three: 3\n"
                               "    two: 2\n"
                               "  [1]:\n"
                               "    four:\n"
                               "      val: 4\n"
                               "  [2]: 1";

  StreamString S;
  object_sp->GetDescription(S);
  EXPECT_EQ(expected, S.GetString());
}

TEST(StructuredDataTest, GetDescriptionFull) {
  Status status;
  std::string input = GetInputFilePath("StructuredData-full.json");
  auto object_sp = StructuredData::ParseJSONFromFile(FileSpec(input), status);
  ASSERT_NE(nullptr, object_sp);

  const std::string expected = "Array:\n"
                               "  [0]: 3.140000\n"
                               "  [1]:\n"
                               "    key: val\n"
                               "Dictionary:\n"
                               "  FalseBool: False\n"
                               "Integer: 1\n"
                               "Null: NULL\n"
                               "String: value\n"
                               "TrueBool: True";

  StreamString S;
  object_sp->GetDescription(S);
  EXPECT_EQ(expected, S.GetString());
}

TEST(StructuredDataTest, ParseJSONFromFile) {
  Status status;
  auto object_sp = StructuredData::ParseJSONFromFile(
      FileSpec("non-existing-file.json"), status);
  EXPECT_EQ(nullptr, object_sp);

  std::string input = GetInputFilePath("StructuredData-basic.json");
  object_sp = StructuredData::ParseJSONFromFile(FileSpec(input), status);
  ASSERT_NE(nullptr, object_sp);

  StreamString S;
  object_sp->Dump(S, false);
  EXPECT_EQ("[1,2,3]", S.GetString());
}

TEST(StructuredDataTest, DictionaryInsertionOrder) {
  StructuredData::Dictionary dict;
  dict.AddIntegerItem("c", 1u);
  dict.AddIntegerItem("a", 2u);
  dict.AddIntegerItem("b", 3u);
  // Replacing an existing value shouldn't change the iteration order.
  dict.AddIntegerItem("a", 4u);

  EXPECT_EQ(3u, dict.GetSize());
  EXPECT_EQ(4u, dict.GetValueForKey("a")->GetUnsignedIntegerValue());
  EXPECT_EQ(nullptr, dict.GetValueForKey("d"));

  std::vector<std::string> keys;
  std::vector<uint64_t> values;
  dict.ForEach([&](llvm::StringRef key, StructuredData::Object *object) {
    keys.push_back(key.str());
    values.push_back(object->GetUnsignedIntegerValue());
    return true;
  });
  EXPECT_EQ((std::vector<std::string>{"c", "a", "b"}), keys);
  EXPECT_EQ((std::vector<uint64_t>{1, 4, 3}), values);

  StructuredData::ArraySP keys_sp = dict.GetKeys();
  ASSERT_EQ(3u, keys_sp->GetSize());
  EXPECT_EQ("c", keys_sp->GetItemAtIndexAsString(0));
  EXPECT_EQ("a", keys_sp->GetItemAtIndexAsString(1));
  EXPECT_EQ("b", keys_sp->GetItemAtIndexAsString(2));

  // Copies should preserve the order too.
  auto dict_sp = std::make_shared<StructuredData::Dictionary>(dict);
  StructuredData::Dictionary copy(dict_sp);
  keys.clear();
  copy.ForEach([&](llvm::StringRef key, StructuredData::Object *) {
    keys.push_back(key.str());
    return true;
  });
  EXPECT_EQ((std::vector<std::string>{"c", "a", "b"}), keys);
  EXPECT_EQ(4u, copy.GetValueForKey("a")->GetUnsignedIntegerValue());
}

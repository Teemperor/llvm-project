//===-- StringPrinterTest.cpp ---------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/Endian.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::formatters;

namespace {
/// A memory source that just stores a string and pretend it's the memory of
/// a process.
struct MockMemorySource : public MemorySource {
  virtual ~MockMemorySource() = default;
  std::string m_data;
  lldb::ByteOrder m_byte_order;
  uint32_t m_addr_size;

  MockMemorySource(const std::string &data, lldb::ByteOrder order,
                   unsigned addr_size)
      : m_data(data), m_byte_order(order), m_addr_size(addr_size) {
    // We currently only support the host byte order (as this is the one we
    // encode our string literals in). StringPrinter anyway doesn't do any
    // byte order handling on its own, so different byte orders aren't really
    // useful here.
    assert(order == endian::InlHostByteOrder() &&
           "only little endian should be used");
    // Address size doesn't really influence this test or the StringPrinter,
    // so let's just make sure we have something reasonable.
    assert(addr_size == 4U && "only 4 byte addresses should be used");
  }

  virtual size_t ReadStringFromMemory(lldb::addr_t vm_addr, char *str,
                                      size_t max_bytes, Status &error,
                                      size_t type_width = 1) {
    std::memset(str, 0, max_bytes);
    std::string terminator(type_width, '\0');
    for (size_t i = 0; i < max_bytes - type_width; ++i) {
      const size_t mem_offset = i + vm_addr;
      assert(mem_offset < m_data.size() && "Out of bounds read?");
      str[i] = m_data.at(mem_offset);
      if (llvm::StringRef(m_data).substr(mem_offset).startswith(terminator))
        return i;
    }
    return max_bytes;
  }

  virtual size_t ReadCStringFromMemory(lldb::addr_t vm_addr, char *cstr,
                                       size_t cstr_max_len, Status &error) {
    for (size_t i = 0; i < cstr_max_len; ++i) {
      char c = m_data.at(i + vm_addr);
      if (c == '\0')
        return i;
      if (i == cstr_max_len)
        break;
      cstr[i] = c;
    }
    return cstr_max_len;
  }

  virtual size_t ReadMemoryFromInferior(lldb::addr_t vm_addr, void *buf,
                                        size_t size, Status &error) {
    assert(size + vm_addr < m_data.size());
    std::memcpy(buf, m_data.data() + vm_addr, size);
    return size;
  }

  virtual lldb::ByteOrder GetByteOrder() const { return m_byte_order; }

  virtual uint32_t GetAddressByteSize() const { return m_addr_size; }
};
} // namespace

#define MEM_STR(DATA) (std::string(DATA, sizeof(DATA)))

namespace {
struct StringPrinterTest : public ::testing::Test {
  /// The string we read into.
  StreamString string;
  /// By default the test sets the location to character position 1 (as 0
  /// is always an invalid location). To disable this for a test case (e.g.,
  /// because the test is testing other locations than 1) set this to false.
  /// This will disable any future updates to the character_pos setting and
  /// disable the automatic padding of the input string to move it to
  /// character position one.
  bool enable_auto_setting_location = true;
  llvm::Optional<size_t> character_pos;
  /// Sets the location of the string start to the given *character* position.
  /// This position is translated into the final byte position by runStringTest
  /// by multiplying it with the size of the character data type (e.g., 1 byte
  /// for ASCII/UTF8, 2 byte for UTF16 and 4 for UTF32).
  void SetCharLocation(size_t pos) {
    if (enable_auto_setting_location)
      character_pos = pos;
  }

  /// Sets up StringPrinter options that we share between buffer and string
  /// reading.
  void initOpts(StringPrinter::DumpToStreamOptions &opts) {
    string.Clear();
    opts.SetStream(&string);
  }

  std::string GetReadData() const { return string.GetString().str(); }

  const lldb::ByteOrder byte_order = endian::InlHostByteOrder();
  const uint32_t addr_size = 4U;
  /// The max size we allow reading from the memory source. Has to be set
  /// alongside the memory source, so we store this here that we can pass it
  /// along when we attach our MockMemorySource.
  size_t max_read_size = 1024;

  /// True iff we expect that the StringPrinter functions return 'true' for
  /// success.
  bool expect_success = true;

  /// The options we use for tringPrinter::ReadStringAndDumpToStream.
  StringPrinter::ReadStringAndDumpToStreamOptions string_opts;
  /// The options we use for tringPrinter::ReadBufferAndDumpToStream.
  StringPrinter::ReadBufferAndDumpToStreamOptions buffer_opts;

  /// Generic function that takes a string-like class and returns its bytes
  /// as a std::string.
  template <class T> std::string toCharString(const T &mem) {
    // Explicitly pass the length in case of embedded \0 bytes in mem.
    return std::string(reinterpret_cast<const char *>(mem.data()),
                       mem.size() * sizeof(typename T::value_type));
  }

  void runUTF8StringAndBufferTest(std::string input, std::string output) {
    SetCharLocation(1);
    runStringTest<std::string, StringPrinter::StringElementType::UTF8>(
        input, output, "u8");
    runBufferTest<std::string, StringPrinter::StringElementType::UTF8>(
        input, output, "u8");
  }

  /// Tries to read the string with the ReadStringAndDumpToStream function.
  /// \param input The input string as any kind of string type.
  /// \param output The expected output as UTF8.
  /// \param CharKindStr A string describing the character kind this test is
  ///                    testing. Used to make any gtest assertion failures
  ///                    more informative.
  template <typename T, StringPrinter::StringElementType CharKind>
  void runStringTest(T input, std::string output, const char *CharKindStr) {
    size_t TypeWidth = sizeof(typename T::value_type);
    initOpts(string_opts);

    std::string mem_bytes = toCharString(input);

    // If the test has set a character positon, translate this now to an
    // actual byte position in memory.
    if (character_pos) {
      string_opts.SetLocation(*character_pos * TypeWidth);
      // Automatically add padding the input so that it starts at character
      // position 1 if we have automatically set the character position to 1.
      // Without this character positon 1 would point to the second character
      // in the input string.
      // See the enable_auto_setting_location flag.
      if (enable_auto_setting_location)
        mem_bytes = std::string(TypeWidth, '_') + mem_bytes;
    }
    // Add enough trailing 0 bytes we need for reading C strings.
    mem_bytes += std::string(TypeWidth, '\0');

    std::unique_ptr<MockMemorySource> src;
    src.reset(new MockMemorySource(mem_bytes, byte_order, addr_size));
    string_opts.setMemorySourceAndMaxSize(src.get(), max_read_size);

    bool b = StringPrinter::ReadStringAndDumpToStream<CharKind>(string_opts);
    EXPECT_EQ(expect_success, b);
    EXPECT_EQ(output, GetReadData()) << "Failed to correctly read string "
                                        "data for "
                                     << CharKindStr;
  }

  /// Tries to read the string with the ReadBufferAndDumpToStream function.
  /// \param input The input string as any kind of string type.
  /// \param output The expected output as UTF8.
  /// \param CharKindStr A string describing the character kind this test is
  ///                    testing. Used to make any gtest assertion failures
  ///                    more informative.
  template <typename T, StringPrinter::StringElementType CharKind>
  void runBufferTest(T input, std::string output, const char *CharKindStr) {
    initOpts(buffer_opts);

    std::string mem = toCharString(input);
    // FIXME: Somehow the StringPrinter only reads the first half of the
    // buffer, so we need to make the read size twice its buffer size to read
    // the whole buffer.
    size_t length_in_bytes = mem.size() * 2U;
    DataExtractor extractor(mem.data(), length_in_bytes, byte_order, addr_size);
    buffer_opts.SetData(extractor);
    bool b = StringPrinter::ReadBufferAndDumpToStream<CharKind>(buffer_opts);
    EXPECT_EQ(expect_success, b);
    EXPECT_EQ(output, GetReadData()) << "Failed to correctly read buffer "
                                        "data for "
                                     << CharKindStr;
  }
};
} // namespace

//------------------------------------------------------------------------------
// Utility macros.
// We have a long range of similar functionality that needs to be tested in this
// test. To remove all the redundant typing of string literals with different
// prefixes, these macros transform a normal string literal to a series of
// test calls with different kinds of unicode/ascii string literals.
// We have to use macros here as we rely on the preprocessor concatenating
// unicode prefixes like u8"" and normal string literals like "foo" into
// a single unicode literal that we can pass to the actual test functions.
// By doing that we can automatically transform "foo" into different encodings.
// E.g. u8"" "foo" -> UTF8
//      u16"" "foo" -> UTF16
//      "foo" -> ASCII

// Transforms the given literal to different unicode variants (UTF8, UTF16,
// UTF32) and also normal ASCII and then tries to read it back via the
// StringPrinter::ReadBufferAndDumpToStream. The result is compared to
// the given UTF8 result.
#define RUN_BUFFER_TEST(literal, result)                                       \
  do {                                                                         \
    runBufferTest<std::string, StringPrinter::StringElementType::ASCII>(       \
        std::string(literal, sizeof(literal)), result, "ascii");               \
    runBufferTest<std::string, StringPrinter::StringElementType::UTF8>(        \
        std::string(u8"" literal, sizeof(u8"" literal)), result, "u8");        \
    runBufferTest<std::u16string, StringPrinter::StringElementType::UTF16>(    \
        std::u16string(u"" literal, sizeof(u"" literal)), result, "u16");      \
    runBufferTest<std::u32string, StringPrinter::StringElementType::UTF32>(    \
        std::u32string(U"" literal, sizeof(U"" literal)), result, "u32");      \
  } while (false)

#define RUN_USTRING_TEST_IMPL(literal, result)                                 \
  runStringTest<std::string, StringPrinter::StringElementType::UTF8>(          \
      std::string(u8"" literal, sizeof(u8"" literal)), result, "u8");          \
  runStringTest<std::u16string, StringPrinter::StringElementType::UTF16>(      \
      std::u16string(u"" literal, sizeof(u"" literal)), result, "u16");        \
  runStringTest<std::u32string, StringPrinter::StringElementType::UTF32>(      \
      std::u32string(U"" literal, sizeof(U"" literal)), result, "u32");

// Transforms the given literal to different unicode variants (UTF8, UTF16,
// UTF32) and then tries to read it back via the
// StringPrinter::ReadStringAndDumpToStream. The result is compared to
// the given UTF8 result.
#define RUN_USTRING_TEST(literal, result)                                      \
  do {                                                                         \
    SetCharLocation(1);                                                        \
    RUN_USTRING_TEST_IMPL(literal, result)                                     \
  } while (false)

#define RUN_ASCII_STRING_TEST(literal, result)                                 \
  do {                                                                         \
    SetCharLocation(1);                                                        \
    runStringTest<std::string, StringPrinter::StringElementType::ASCII>(       \
        std::string(literal, sizeof(literal)), result, "ascii");               \
  } while (false)

// Same as RUN_USTRING_TEST but also tests the literal as plain ASCII.
#define RUN_STRING_TEST(literal, result)                                       \
  do {                                                                         \
    SetCharLocation(1);                                                        \
    RUN_ASCII_STRING_TEST(literal, result);                                    \
    RUN_USTRING_TEST_IMPL(literal, result);                                    \
  } while (false)

// Runs both RUN_STRING_TEST and RUN_BUFFER_TEST with the given arguments.
#define RUN_STRING_AND_BUFFER_TEST(literal, result)                            \
  do {                                                                         \
    RUN_STRING_TEST(literal, result);                                          \
    RUN_BUFFER_TEST(literal, result);                                          \
  } while (false)

//------------------------------------------------------------------------------
// Location settings tests.

TEST_F(StringPrinterTest, ZeroPosition) {
  // Disable that we automatically set the location to character position 1.
  enable_auto_setting_location = false;
  string_opts.SetLocation(0U);
  expect_success = false;
  RUN_STRING_TEST("A", "");
}

TEST_F(StringPrinterTest, InvalidPosition) {
  // Disable that we automatically set the location to character position 1.
  enable_auto_setting_location = false;
  string_opts.SetLocation(LLDB_INVALID_ADDRESS);
  expect_success = false;
  RUN_STRING_TEST("C", "");
}

TEST_F(StringPrinterTest, OtherPosition) {
  // All tests use character position 1 by default, this just tests that
  // character position 2 is also working. See enable_auto_setting_location.
  SetCharLocation(2U);
  // Disable that we automatically set the location to character position 1.
  // We do this after setting our character position to prevent any future
  // changes to our set character position.
  enable_auto_setting_location = false;
  RUN_STRING_TEST("ABC", "\"C\"");
}

//------------------------------------------------------------------------------
// Generic tests with a few strings.

TEST_F(StringPrinterTest, NormalString) {
  RUN_STRING_AND_BUFFER_TEST("foobar", "\"foobar\"");
}

TEST_F(StringPrinterTest, LatinAlphabetSmallLetters) {
  RUN_STRING_AND_BUFFER_TEST("abcdefghijklmnopqrstuvwxyz", //
                             "\"abcdefghijklmnopqrstuvwxyz\"");
}

TEST_F(StringPrinterTest, LatinAlphabetCapitalLetters) {
  RUN_STRING_AND_BUFFER_TEST("ABCDEFGHIJKLMNOPQRSTUVWXYZ", //
                             "\"ABCDEFGHIJKLMNOPQRSTUVWXYZ\"");
}

TEST_F(StringPrinterTest, Numbers) {
  RUN_STRING_AND_BUFFER_TEST("1234567890", "\"1234567890\"");
}

TEST_F(StringPrinterTest, EmptyString) {
  RUN_STRING_AND_BUFFER_TEST("", "\"\"");
}

//------------------------------------------------------------------------------
// Unicode tests

TEST_F(StringPrinterTest, UnicodeLetters) {
  RUN_USTRING_TEST("\u01AA", //
                   u8"\"\xC6\xAA\"");

  // Test a charaters that are corner cases of our printable/nonprintable
  // checks.
  RUN_USTRING_TEST("\u2027", u8"\"\xE2\x80\xA7\"");

  RUN_USTRING_TEST("\u2028", u8"\"\\U00002028\"");

  RUN_USTRING_TEST("\u2029", u8"\"\\U00002029\"");

  RUN_USTRING_TEST("\u202A", u8"\"\\U0000202a\"");

  RUN_USTRING_TEST("\uFFFE", u8"\"\\U0000fffe\"");
}

TEST_F(StringPrinterTest, UTF8CornerCases) {
  // Tests the corner cases of UTF8 codepoint decoding.
  // Max codepoint in 1 bytes.
  RUN_USTRING_TEST("\u007F", u8"\"\\x7f\"");
  // Min codepoint in 2 bytes.
  RUN_USTRING_TEST("\u0080", u8"\"\\U00000080\"");
  // Max codepoint in 2 bytes.
  RUN_USTRING_TEST("\u07FF", u8"\"\xDF\xBF\"");
  // Min codepoint in 3 bytes.
  RUN_USTRING_TEST("\u0800", u8"\"\xE0\xA0\x80\"");
  // Max codepoint in 3 bytes.
  RUN_USTRING_TEST("\uFFFF", u8"\"\\U0000ffff\"");
  // Min codepoint in 4 bytes.
  RUN_USTRING_TEST("\U00010000", "\"\xF0\\x90\\x80\\x80\"");
  // Max codepoint in 4 bytes.
  RUN_USTRING_TEST("\U0010FFFF", "\"\xF4\x8F\xBF\xBF\"");
}

//------------------------------------------------------------------------------
// Corrupted UTF8 tests.
// These tests take valid UTF8 and then remove bytes from the start or end
// to generate corrupted UTF8 strings. We mostly care about the tests that
// remove trailing bytes as this tests that we not just blindly trust the
// UTF8 prefix code when reading from memory.

// Allow converting literals to std::string even if they have embedded zeroes.
// FIXME: Remove this, only needed to provide the result strings for currently
// broken UTF8 test cases.
#define STR_WITH_ZEROES(literal) (std::string(literal, sizeof(literal) - 1U))

TEST_F(StringPrinterTest, UTF8MissingStart4Bytes) {
  std::string utf8 = u8"\U0010FFFE";
  assert(utf8.size() == 4);
  utf8.erase(0, 1);
  runUTF8StringAndBufferTest(utf8, "\"\\x8f\\xbf\\xbe\"");
  utf8.erase(0, 1);
  runUTF8StringAndBufferTest(utf8, "\"\\xbf\\xbe\"");
  utf8.erase(0, 1);
  runUTF8StringAndBufferTest(utf8, "\"\\xbe\"");
}

TEST_F(StringPrinterTest, UTF8MissingEnd4Bytes) {
  std::string utf8 = u8"\U0010FFFE";
  assert(utf8.size() == 4);
  // FIXME: This shouldn't add the found 0 terminator to the output.
  utf8.erase(3, 1);
  runUTF8StringAndBufferTest(utf8, STR_WITH_ZEROES("\"\xF4\x8F\xBF\0\""));
  utf8.erase(2, 1);
  runUTF8StringAndBufferTest(utf8, STR_WITH_ZEROES("\"\xF4\x8F\0\""));
  utf8.erase(1, 1);
  runUTF8StringAndBufferTest(utf8, STR_WITH_ZEROES("\"\xF4\0\""));
}

TEST_F(StringPrinterTest, UTF8MissingStart3Bytes) {
  std::string utf8 = u8"\u0800";
  assert(utf8.size() == 3);
  utf8.erase(0, 1);
  runUTF8StringAndBufferTest(utf8, "\"\\xa0\\x80\"");
  utf8.erase(0, 1);
  runUTF8StringAndBufferTest(utf8, "\"\\x80\"");
}

TEST_F(StringPrinterTest, UTF8MissingEnd3Bytes) {
  std::string utf8 = u8"\u0800";
  assert(utf8.size() == 3);
  // FIXME: This shouldn't add the unexpected 0 terminator to the output.
  utf8.erase(2, 1);
  runUTF8StringAndBufferTest(utf8, STR_WITH_ZEROES("\"\xE0\xA0\0\""));
  utf8.erase(1, 1);
  runUTF8StringAndBufferTest(utf8, STR_WITH_ZEROES("\"\xE0\0\""));
}

TEST_F(StringPrinterTest, UTF8MissingStart2Bytes) {
  std::string utf8 = u8"\u0080";
  assert(utf8.size() == 2);
  utf8.erase(0, 1);
  runUTF8StringAndBufferTest(utf8, "\"\\x80\"");
}

TEST_F(StringPrinterTest, UTF8MissingEnd2Bytes) {
  std::string utf8 = u8"\u0080";
  assert(utf8.size() == 2);
  utf8.erase(1, 1);
  runUTF8StringAndBufferTest(utf8, "\"\xC2\"");
}

//------------------------------------------------------------------------------
// Source size and zero terminated result tests.

TEST_F(StringPrinterTest, SourceSizeZeroWithoutTerminator) {
  // FIXME: That disables source size.
  string_opts.SetSourceSize(0);
  string_opts.SetNeedsZeroTermination(false);
  buffer_opts.SetSourceSize(0);
  buffer_opts.SetNeedsZeroTermination(false);
  RUN_STRING_AND_BUFFER_TEST("abcd", "\"abcd\"");
}

TEST_F(StringPrinterTest, SourceSizeZeroWithTerminator) {
  // FIXME: That disables source size.
  string_opts.SetSourceSize(0);
  string_opts.SetNeedsZeroTermination(true);
  buffer_opts.SetSourceSize(0);
  buffer_opts.SetNeedsZeroTermination(true);
  RUN_STRING_AND_BUFFER_TEST("abcd", "\"abcd\"");
}

TEST_F(StringPrinterTest, MatchingSourceSizeWithoutTerminator) {
  string_opts.SetSourceSize(3);
  string_opts.SetNeedsZeroTermination(false);
  buffer_opts.SetSourceSize(3);
  buffer_opts.SetNeedsZeroTermination(false);
  RUN_STRING_AND_BUFFER_TEST("abcd", "\"abc\"");
}

TEST_F(StringPrinterTest, TooShortSourceSizeWithoutTerminator) {
  string_opts.SetSourceSize(3);
  string_opts.SetNeedsZeroTermination(false);
  buffer_opts.SetSourceSize(3);
  buffer_opts.SetNeedsZeroTermination(false);
  RUN_STRING_AND_BUFFER_TEST("abcdef", "\"abc\"");
}

TEST_F(StringPrinterTest, TooLongSourceSizeWithoutTerminator) {
  string_opts.SetSourceSize(8);
  string_opts.SetNeedsZeroTermination(false);
  buffer_opts.SetSourceSize(8);
  buffer_opts.SetNeedsZeroTermination(false);
  RUN_STRING_AND_BUFFER_TEST("abc\0cdefadsf", "\"abc\"");
}

TEST_F(StringPrinterTest, MatchingSourceSizeWithTerminator) {
  string_opts.SetSourceSize(3);
  string_opts.SetNeedsZeroTermination(true);
  buffer_opts.SetSourceSize(3);
  buffer_opts.SetNeedsZeroTermination(true);
  // FIXME: Seems like we forgot to implement zero terminator support for
  // ASCII.
  RUN_USTRING_TEST("abcd", "\"ab\"");
  RUN_ASCII_STRING_TEST("abcd", "\"abc\"");
  RUN_BUFFER_TEST("abcd", "\"abc\"");
}

TEST_F(StringPrinterTest, TooShortSourceSizeWithTerminator) {
  string_opts.SetSourceSize(3);
  string_opts.SetNeedsZeroTermination(true);
  buffer_opts.SetSourceSize(3);
  buffer_opts.SetNeedsZeroTermination(true);
  // FIXME: Seems like we forgot to implement zero terminator support for
  // ASCII.
  RUN_USTRING_TEST("abcdef", "\"ab\"");
  RUN_ASCII_STRING_TEST("abcd", "\"abc\"");
  RUN_BUFFER_TEST("abcdef", "\"abc\"");
}

TEST_F(StringPrinterTest, TooLongSourceSizeWithTerminator) {
  string_opts.SetSourceSize(8);
  string_opts.SetNeedsZeroTermination(true);
  buffer_opts.SetSourceSize(8);
  buffer_opts.SetNeedsZeroTermination(true);
  RUN_STRING_AND_BUFFER_TEST("abc", "\"abc\"");
}

//------------------------------------------------------------------------------
// Ignore zero terminator tests.

TEST_F(StringPrinterTest, IgnoredZeroAfterString) {
  string_opts.SetSourceSize(2);
  string_opts.SetBinaryZeroIsTerminator(false);
  string_opts.SetNeedsZeroTermination(true);
  buffer_opts.SetSourceSize(2);
  buffer_opts.SetBinaryZeroIsTerminator(false);
  buffer_opts.SetNeedsZeroTermination(true);
  // FIXME: It seems we terminate unicode string in this mode with an actual
  // '0' *character* and not a '\0' *byte*...
  RUN_USTRING_TEST("abc\0adsfasdf", "\"a\\0\"");
  RUN_ASCII_STRING_TEST("abc\0adsfasdf", "\"ab\"");
  RUN_BUFFER_TEST("abc\0adsfasdf", "\"ab\"");
}

TEST_F(StringPrinterTest, DontIgnoreEmbeddedZero) {
  string_opts.SetSourceSize(8);
  string_opts.SetBinaryZeroIsTerminator(true);
  buffer_opts.SetSourceSize(8);
  buffer_opts.SetBinaryZeroIsTerminator(true);
  RUN_STRING_AND_BUFFER_TEST("abc\0adsfasdf", "\"abc\"");
}

TEST_F(StringPrinterTest, IgnoreEmbeddedZero) {
  string_opts.SetSourceSize(8);
  string_opts.SetBinaryZeroIsTerminator(false);
  buffer_opts.SetSourceSize(8);
  buffer_opts.SetBinaryZeroIsTerminator(false);
  // FIXME: Unicode ignores embedded zero, but just keeps adding escaped zeroes?
  RUN_USTRING_TEST("abc\0adsfasdf", "\"abc\\0\\0\\0\\0\\0\"");
  // FIXME: ASCII doesn't even support ignoring the embedded zero...
  RUN_ASCII_STRING_TEST("abc\0adsfasdf", "\"abc\"");
  RUN_BUFFER_TEST("abc\0adsfasdf", "\"abc\\0adsf\"");
}

//------------------------------------------------------------------------------
// ASCII escaping tests

TEST_F(StringPrinterTest, Escape) {
  RUN_STRING_AND_BUFFER_TEST("\n\"\t\r\b\a\f\v", //
                             "\"\\n\\\"\\t\\r\\b\\a\\f\\v\"");
}

//------------------------------------------------------------------------------
// String prefix tests

TEST_F(StringPrinterTest, Prefix) {
  string_opts.SetPrefixToken("PREFIX");
  buffer_opts.SetPrefixToken("PREFIX");
  RUN_STRING_AND_BUFFER_TEST("", "PREFIX\"\"");
}

TEST_F(StringPrinterTest, PrefixEmptyString) {
  string_opts.SetPrefixToken("PREFIX");
  buffer_opts.SetPrefixToken("PREFIX");
  RUN_STRING_AND_BUFFER_TEST("", "PREFIX\"\"");
}

TEST_F(StringPrinterTest, EmptyPrefix) {
  string_opts.SetPrefixToken("");
  buffer_opts.SetPrefixToken("");
  RUN_STRING_AND_BUFFER_TEST("", "\"\"");
}

TEST_F(StringPrinterTest, EmptyPrefixEmptyString) {
  string_opts.SetPrefixToken("");
  buffer_opts.SetPrefixToken("");
  RUN_STRING_AND_BUFFER_TEST("", "\"\"");
}

//------------------------------------------------------------------------------
// String suffix tests

TEST_F(StringPrinterTest, Suffix) {
  SetCharLocation(1);
  string_opts.SetSuffixToken("SUFFIX");
  buffer_opts.SetSuffixToken("SUFFIX");
  RUN_STRING_AND_BUFFER_TEST("", "\"\"SUFFIX");
}

TEST_F(StringPrinterTest, SuffixEmptyString) {
  SetCharLocation(1);
  string_opts.SetSuffixToken("SUFFIX");
  buffer_opts.SetSuffixToken("SUFFIX");
  RUN_STRING_AND_BUFFER_TEST("", "\"\"SUFFIX");
}

TEST_F(StringPrinterTest, SuffixPrefix) {
  string_opts.SetSuffixToken("");
  buffer_opts.SetSuffixToken("");
  RUN_STRING_AND_BUFFER_TEST("", "\"\"");
}

TEST_F(StringPrinterTest, SuffixPrefixEmptyString) {
  string_opts.SetSuffixToken("");
  buffer_opts.SetSuffixToken("");
  RUN_STRING_AND_BUFFER_TEST("", "\"\"");
}

//------------------------------------------------------------------------------
// Max size setting tests.

TEST_F(StringPrinterTest, MaxSizeWithoutTerminator) {
  max_read_size = 3;
  string_opts.SetIgnoreMaxLength(false);
  string_opts.SetNeedsZeroTermination(false);
  // FIXME: ASCII and Unicode don't agree on the output string length.
  RUN_USTRING_TEST("1234", "\"12\"");
  RUN_ASCII_STRING_TEST("1234", "\"123\"");
}

TEST_F(StringPrinterTest, MaxSizeWithTerminator) {
  max_read_size = 3;
  string_opts.SetIgnoreMaxLength(false);
  string_opts.SetNeedsZeroTermination(true);
  // FIXME: ASCII and Unicode don't agree on the output string length.
  RUN_USTRING_TEST("1234", "\"12\"");
  RUN_ASCII_STRING_TEST("1234", "\"123\"");
}

TEST_F(StringPrinterTest, MaxSizeWithoutTerminatorAndShorterSourceLength) {
  max_read_size = 3;
  string_opts.SetIgnoreMaxLength(false);
  string_opts.SetNeedsZeroTermination(false);
  string_opts.SetSourceSize(1);
  RUN_STRING_TEST("1234", "\"1\"");
}

TEST_F(StringPrinterTest, MaxSizeWithTerminatorAndShorterSourceLength) {
  max_read_size = 3;
  string_opts.SetIgnoreMaxLength(false);
  string_opts.SetNeedsZeroTermination(true);
  string_opts.SetSourceSize(1);
  // FIXME: ASCII and Unicode don't agree on the output string length.
  RUN_USTRING_TEST("1234", "\"\"");
  RUN_ASCII_STRING_TEST("1234", "\"1\"");
}

TEST_F(StringPrinterTest, MaxSizeWithoutTerminatorAndSameSourceLength) {
  max_read_size = 3;
  string_opts.SetIgnoreMaxLength(false);
  string_opts.SetNeedsZeroTermination(false);
  string_opts.SetSourceSize(3);
  RUN_STRING_TEST("1234", "\"123\"");
}

TEST_F(StringPrinterTest, MaxSizeWithTerminatorAndSameSourceLength) {
  max_read_size = 3;
  string_opts.SetIgnoreMaxLength(false);
  string_opts.SetNeedsZeroTermination(true);
  string_opts.SetSourceSize(3);
  // FIXME: ASCII and Unicode don't agree on the output string length.
  RUN_USTRING_TEST("1234", "\"12\"");
  RUN_ASCII_STRING_TEST("1234", "\"123\"");
}

TEST_F(StringPrinterTest, MaxSizeWithoutTerminatorAndLongerSourceLength) {
  max_read_size = 3;
  string_opts.SetIgnoreMaxLength(false);
  string_opts.SetNeedsZeroTermination(false);
  string_opts.SetSourceSize(4);
  RUN_STRING_TEST("1234", "\"123\"...");
}

TEST_F(StringPrinterTest, MaxSizeWithTerminatorAndLongerSourceLength) {
  max_read_size = 3;
  string_opts.SetIgnoreMaxLength(false);
  string_opts.SetNeedsZeroTermination(true);
  string_opts.SetSourceSize(4);
  // FIXME: ASCII and Unicode don't agree on the output string length.
  RUN_USTRING_TEST("1234", "\"12\"...");
  RUN_ASCII_STRING_TEST("1234", "\"123\"...");
}

//------------------------------------------------------------------------------
// Truncated buffer.

TEST_F(StringPrinterTest, IsTruncated) {
  buffer_opts.SetIsTruncated(true);
  RUN_BUFFER_TEST("1234", "\"1234\"...");
}

TEST_F(StringPrinterTest, IsNotTruncated) {
  buffer_opts.SetIsTruncated(false);
  RUN_BUFFER_TEST("1234", "\"1234\"");
}

//------------------------------------------------------------------------------
// Language tests

TEST_F(StringPrinterTest, LanguageC) {
  string_opts.SetLanguage(lldb::eLanguageTypeC);
  buffer_opts.SetLanguage(lldb::eLanguageTypeC);
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "\" \\t@\\\"'\\\\\"");
}

TEST_F(StringPrinterTest, LanguageC11) {
  string_opts.SetLanguage(lldb::eLanguageTypeC11);
  buffer_opts.SetLanguage(lldb::eLanguageTypeC11);
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "\" \\t@\\\"'\\\\\"");
}

TEST_F(StringPrinterTest, LanguageObjCxx) {
  string_opts.SetLanguage(lldb::eLanguageTypeObjC_plus_plus);
  buffer_opts.SetLanguage(lldb::eLanguageTypeObjC_plus_plus);
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "\" \\t@\\\"'\\\\\"");
}

TEST_F(StringPrinterTest, LanguageObjC) {
  string_opts.SetLanguage(lldb::eLanguageTypeObjC);
  buffer_opts.SetLanguage(lldb::eLanguageTypeObjC);
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "\" \\t@\\\"'\\\\\"");
}

TEST_F(StringPrinterTest, LanguageSwift) {
  string_opts.SetLanguage(lldb::eLanguageTypeSwift);
  buffer_opts.SetLanguage(lldb::eLanguageTypeSwift);
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "\" \\t@\\\"'\\\\\"");
}

TEST_F(StringPrinterTest, LanguageUnknown) {
  string_opts.SetLanguage(lldb::eLanguageTypeUnknown);
  buffer_opts.SetLanguage(lldb::eLanguageTypeUnknown);
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "\" \\t@\\\"'\\\\\"");
}

TEST_F(StringPrinterTest, LanguageUnimplemented) {
  // Not implemented, but we should at least do something reasonable.
  string_opts.SetLanguage(lldb::eLanguageTypeModula2);
  buffer_opts.SetLanguage(lldb::eLanguageTypeModula2);
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "\" \\t@\\\"'\\\\\"");
}

//------------------------------------------------------------------------------
// Quote character tests

TEST_F(StringPrinterTest, SingleQuoteAsQuote) {
  SetCharLocation(1);
  string_opts.SetQuote('\'');
  buffer_opts.SetQuote('\'');
  // FIXME: We should probably escape ' when we use it as a quote character
  // (otherwise printing a `char c = '\''` produces ''' as value.
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "' \\t@\\\"'\\\\'");
}

TEST_F(StringPrinterTest, LetterAsQuote) {
  string_opts.SetQuote('a');
  buffer_opts.SetQuote('a');
  RUN_STRING_AND_BUFFER_TEST(" \t@\"'\\\0", "a \\t@\\\"'\\\\a");
}

TEST_F(StringPrinterTest, NoQuoteCharacter) {
  SetCharLocation(1);
  // Disables quoting.
  string_opts.SetQuote('\0');
  buffer_opts.SetQuote('\0');
  // FIXME: Disabling quoting when printing ASCII instead actually quotes the
  // string with NULL bytes. For now only run this with unicode strings where
  // the quoting isn't doing that. We can't really test this as a \0-quoted
  // string is just an empty string for the Stream we use...
  RUN_USTRING_TEST(" \t@\"'\\\0", " \\t@\\\"'\\\\");
  RUN_BUFFER_TEST(" \t@\"'\\\0", " \\t@\\\"'\\\\");
}

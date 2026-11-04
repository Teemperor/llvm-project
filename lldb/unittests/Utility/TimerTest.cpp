//===-- TimerTest.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Timer.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"
#include "gtest/gtest.h"
#include <optional>
#include <thread>

using namespace lldb_private;

namespace {
struct CategoryStats {
  double seconds;
  double total;
  double child;
  int count;
};

/// Finds the line describing \p category in a DumpCategoryTimes() dump and
/// parses its statistics. A line looks like:
///   0.105202764 sec (total: 0.132s; child: 0.027s; count: 1) for TimerTest.A
std::optional<CategoryStats> ParseCategory(llvm::StringRef dump,
                                           llvm::StringRef category) {
  llvm::Regex line_pattern(R"(^([0-9.]+) sec \(total: ([0-9.]+)s; )"
                           R"(child: ([0-9.]+)s; count: ([0-9]+)\) for (.+)$)");
  for (llvm::StringRef line : llvm::split(dump, '\n')) {
    llvm::SmallVector<llvm::StringRef, 6> matches;
    if (!line_pattern.match(line.trim(), &matches))
      continue;
    if (matches[5] != category)
      continue;
    CategoryStats stats;
    if (matches[1].getAsDouble(stats.seconds) ||
        matches[2].getAsDouble(stats.total) ||
        matches[3].getAsDouble(stats.child) ||
        matches[4].getAsInteger(10, stats.count))
      return std::nullopt;
    return stats;
  }
  return std::nullopt;
}

constexpr Timer::CategoryID g_cat_a = timers::TimerTest::A;
constexpr Timer::CategoryID g_cat_b = timers::TimerTest::B;
} // namespace

TEST(TimerTest, CategoryTimes) {
  Timer::ResetCategoryTimes();
  {
    Timer t(g_cat_a, ".");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  StreamString ss;
  Timer::DumpCategoryTimes(ss);
  std::optional<CategoryStats> cat_a =
      ParseCategory(ss.GetString(), "TimerTest.A");
  ASSERT_TRUE(cat_a.has_value()) << "String: " << ss.GetData();
  EXPECT_LT(0.001, cat_a->seconds);
  EXPECT_GT(0.1, cat_a->seconds);
}

TEST(TimerTest, CategoryTimesNested) {
  Timer::ResetCategoryTimes();
  {
    Timer t1(g_cat_a, ".");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Explicitly testing the same category as above.
    Timer t2(g_cat_a, ".");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  StreamString ss;
  Timer::DumpCategoryTimes(ss);
  // It should only appear once.
  ASSERT_EQ(ss.GetString().count("TimerTest.A"), 1U);
  std::optional<CategoryStats> cat_a =
      ParseCategory(ss.GetString(), "TimerTest.A");
  ASSERT_TRUE(cat_a.has_value()) << "String: " << ss.GetData();
  EXPECT_LT(0.002, cat_a->seconds);
  EXPECT_GT(0.2, cat_a->seconds);
}

TEST(TimerTest, CategoryTimes2) {
  Timer::ResetCategoryTimes();
  {
    Timer t1(g_cat_a, ".");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Timer t2(g_cat_b, ".");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  StreamString ss;
  Timer::DumpCategoryTimes(ss);
  std::optional<CategoryStats> cat_a =
      ParseCategory(ss.GetString(), "TimerTest.A");
  std::optional<CategoryStats> cat_b =
      ParseCategory(ss.GetString(), "TimerTest.B");
  ASSERT_TRUE(cat_a.has_value()) << "String: " << ss.GetData();
  ASSERT_TRUE(cat_b.has_value()) << "String: " << ss.GetData();
  EXPECT_LT(0.01, cat_a->seconds);
  EXPECT_GT(1, cat_a->seconds);
  EXPECT_LT(0.001, cat_b->seconds);
  EXPECT_GT(0.1, cat_b->seconds);
}

TEST(TimerTest, CategoryTimesStats) {
  Timer::ResetCategoryTimes();
  {
    Timer t1(g_cat_a, ".");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
      Timer t2(g_cat_b, ".");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    {
      Timer t3(g_cat_b, ".");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  StreamString ss;
  Timer::DumpCategoryTimes(ss);
  std::optional<CategoryStats> cat_a =
      ParseCategory(ss.GetString(), "TimerTest.A");
  std::optional<CategoryStats> cat_b =
      ParseCategory(ss.GetString(), "TimerTest.B");
  ASSERT_TRUE(cat_a.has_value()) << "String: " << ss.GetData();
  ASSERT_TRUE(cat_b.has_value()) << "String: " << ss.GetData();
  EXPECT_NEAR(cat_a->total - cat_a->child, cat_a->seconds, 0.002);
  EXPECT_EQ(1, cat_a->count);
  EXPECT_NEAR(cat_a->child, cat_b->seconds, 0.002);
  EXPECT_EQ(2, cat_b->count);
}

TEST(TimerTest, DumpSortedByName) {
  Timer::ResetCategoryTimes();
  {
    // Spend more time in TimerTest.B so that sorting by time would put it
    // first.
    Timer t(g_cat_b);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  {
    Timer t(g_cat_a);
  }
  StreamString ss;
  Timer::DumpCategoryTimes(ss);
  size_t pos_a = ss.GetString().find("for TimerTest.A\n");
  size_t pos_b = ss.GetString().find("for TimerTest.B\n");
  ASSERT_NE(llvm::StringRef::npos, pos_a) << "String: " << ss.GetData();
  ASSERT_NE(llvm::StringRef::npos, pos_b) << "String: " << ss.GetData();
  EXPECT_LT(pos_a, pos_b);
}

TEST(TimerTest, CategoryInfo) {
  const TimerCategoryInfo &info = Timer::GetCategoryInfo(g_cat_b);
  EXPECT_EQ("TimerTest.B", info.name);

  EXPECT_EQ("Module.GetObjectFile",
            Timer::GetCategoryInfo(timers::Module::GetObjectFile).name);
}

TEST(TimerTest, ScopedTimer) {
  Timer::ResetCategoryTimes();
  {
    LLDB_SCOPED_TIMER(TimerTest::A);
  }
  {
    LLDB_SCOPED_TIMERF(TimerTest::A, "(%d)", 1);
  }
  {
    LLDB_SCOPED_TIMERV(TimerTest::A, "({0})", 2);
  }

  StreamString ss;
  Timer::DumpCategoryTimes(ss);
  std::optional<CategoryStats> cat_a =
      ParseCategory(ss.GetString(), "TimerTest.A");
  ASSERT_TRUE(cat_a.has_value()) << "String: " << ss.GetData();
  EXPECT_EQ(3, cat_a->count);
}

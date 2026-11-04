//===-- Timer.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_TIMER_H
#define LLDB_UTILITY_TIMER_H

#include "lldb/lldb-defines.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/FormatVariadic.h"
#include <atomic>
#include <cstdint>
#include <vector>

namespace lldb_private {
class Stream;

/// Static information about a timer category. All timer categories are
/// defined in lldb/Utility/TimerCategories.td.
struct TimerCategoryInfo {
  /// The full category name, e.g. "Module.GetObjectFile".
  llvm::StringLiteral name;
  /// The dot-separated components of the name, e.g. {"Module",
  /// "GetObjectFile"}. They determine the nesting of the timer in the
  /// "statistics dump" JSON.
  llvm::ArrayRef<llvm::StringLiteral> components;
};

/// \class Timer Timer.h "lldb/Utility/Timer.h"
/// A timer class that simplifies common timing metrics.

class Timer {
public:
  /// Identifies a timer category. Every category defined in
  /// TimerCategories.td has a constant in the lldb_private::timers namespace,
  /// e.g. "Module.GetObjectFile" is timers::Module::GetObjectFile.
  enum class CategoryID : uint32_t {};

  static const TimerCategoryInfo &GetCategoryInfo(CategoryID id);

  /// Starts a timer that is only described by its category name.
  explicit Timer(CategoryID category);

  /// Starts a timer with a message that is printed after the category name.
  Timer(CategoryID category, const char *format, ...)
#if !defined(_MSC_VER)
      // MSVC appears to have trouble recognizing the this argument in the
      // constructor.
      __attribute__((format(printf, 3, 4)))
#endif
      ;

  Timer(CategoryID category, const llvm::formatv_object_base &obj);

  /// Destructor
  ~Timer();

  void Dump();

  static void SetDisplayDepth(uint32_t depth);

  static void SetQuiet(bool value);

  static void DumpCategoryTimes(Stream &s);

  static void ResetCategoryTimes();

  /// Accumulated statistics of a timer category.
  struct CategoryStats {
    const TimerCategoryInfo *info = nullptr;
    /// Number of times a timer of this category finished.
    uint64_t count = 0;
    /// Time spent excluding time spent in nested timers.
    uint64_t nanos = 0;
    /// Time spent including time spent in nested timers. The min/max values
    /// below are also inclusive.
    uint64_t nanos_total = 0;
    uint64_t nanos_min = 0;
    uint64_t nanos_max = 0;
  };

  /// Returns the statistics of every category that recorded at least one
  /// timer run, sorted by category name.
  static std::vector<CategoryStats> GetCategoryStats();

protected:
  using TimePoint = std::chrono::steady_clock::time_point;
  void ChildDuration(TimePoint::duration dur) { m_child_duration += dur; }

  void Start(llvm::function_ref<std::string()> start_message_func);

  CategoryID m_category;
  TimePoint m_total_start;
  TimePoint::duration m_child_duration{0};

  static std::atomic<bool> g_quiet;
  static std::atomic<unsigned> g_display_depth;

private:
  Timer(const Timer &) = delete;
  const Timer &operator=(const Timer &) = delete;
};

/// The CategoryID constants of all timer categories, nested in namespaces that
/// mirror the dot-separated category names.
namespace timers {
#include "lldb/Utility/TimerCategories.inc"
} // namespace timers

} // namespace lldb_private

// The category is the path of a timer category in the lldb_private::timers
// namespace, e.g. Module::GetObjectFile for the category "Module.GetObjectFile"
// defined in lldb/Utility/TimerCategories.td. The category name identifies the
// timer in the "log timers dump" output and its dot-separated components
// determine the nesting in the "statistics dump" JSON. When timers are
// displayed, the category name is printed followed by the optional message, so
// the message should only contain additional details.
#define LLDB_SCOPED_TIMER(category)                                            \
  ::lldb_private::Timer _scoped_timer{::lldb_private::timers::category}
#define LLDB_SCOPED_TIMERF(category, ...)                                      \
  ::lldb_private::Timer _scoped_timer(::lldb_private::timers::category,        \
                                      __VA_ARGS__)
#define LLDB_SCOPED_TIMERV(category, ...)                                      \
  ::lldb_private::Timer _scoped_timer(::lldb_private::timers::category,        \
                                      llvm::formatv(__VA_ARGS__))

#endif // LLDB_UTILITY_TIMER_H

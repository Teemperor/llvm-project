//===-- Timer.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "lldb/Utility/Timer.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Signposts.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include <cassert>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

using namespace lldb_private;

// Defines g_categories, the info of each timer category indexed by its ID.
#include "lldb/Utility/TimerCategoryInfos.inc"

#define TIMER_INDENT_AMOUNT 2

namespace {
typedef std::vector<Timer *> TimerStack;

/// The accumulated times of a single timer category.
struct CategoryCounters {
  /// Time spent in this category excluding time spent in nested timers.
  std::atomic<uint64_t> nanos{0};
  /// Time spent in this category including time spent in nested timers.
  std::atomic<uint64_t> nanos_total{0};
  std::atomic<uint64_t> count{0};

  void Reset() {
    nanos.store(0, std::memory_order_release);
    nanos_total.store(0, std::memory_order_release);
    count.store(0, std::memory_order_release);
  }
};
} // end of anonymous namespace

/// The counters of each timer category indexed by its ID.
static CategoryCounters g_counters[std::size(g_categories)];

/// Allows llvm::Timer to emit signposts when supported.
static llvm::ManagedStatic<llvm::SignpostEmitter> Signposts;

std::atomic<bool> Timer::g_quiet(true);
std::atomic<unsigned> Timer::g_display_depth(0);
static std::mutex &GetFileMutex() {
  static std::mutex *g_file_mutex_ptr = new std::mutex();
  return *g_file_mutex_ptr;
}

static TimerStack &GetTimerStackForCurrentThread() {
  static thread_local TimerStack g_stack;
  return g_stack;
}

const TimerCategoryInfo &Timer::GetCategoryInfo(CategoryID id) {
  return g_categories[static_cast<size_t>(id)];
}

void Timer::SetQuiet(bool value) { g_quiet = value; }

void Timer::Start(llvm::function_ref<std::string()> start_message_func) {
  const char *name = GetCategoryInfo(m_category).name.data();
  Signposts->startInterval(this, name);
  TimerStack &stack = GetTimerStackForCurrentThread();

  stack.push_back(this);
  if (!g_quiet && stack.size() <= g_display_depth) {
    std::lock_guard<std::mutex> lock(GetFileMutex());

    // Indent
    ::fprintf(stdout, "%*s", int(stack.size() - 1) * TIMER_INDENT_AMOUNT, "");
    // Print the category name followed by the formatted message (if any).
    ::fprintf(stdout, "%s", name);
    const std::string start_message = start_message_func();
    if (!start_message.empty())
      ::fprintf(stdout, " %s", start_message.c_str());
    // Newline
    ::fprintf(stdout, "\n");
  }
}

Timer::Timer(CategoryID category)
    : m_category(category), m_total_start(std::chrono::steady_clock::now()) {
  Start([]() { return std::string(); });
}

Timer::Timer(CategoryID category, const char *format, ...)
    : m_category(category), m_total_start(std::chrono::steady_clock::now()) {
  va_list args;
  va_start(args, format);
  Start([&]() -> std::string {
    StreamString sstr;
    sstr.PrintfVarArg(format, args);

    return sstr.GetString().str();
  });
  va_end(args);
}

Timer::Timer(CategoryID category, const llvm::formatv_object_base &obj)
    : m_category(category), m_total_start(std::chrono::steady_clock::now()) {
  Start([&]() -> std::string { return obj.str(); });
}

Timer::~Timer() {
  using namespace std::chrono;

  auto stop_time = steady_clock::now();
  auto total_dur = stop_time - m_total_start;
  auto timer_dur = total_dur - m_child_duration;

  Signposts->endInterval(this, GetCategoryInfo(m_category).name);

  TimerStack &stack = GetTimerStackForCurrentThread();
  if (!g_quiet && stack.size() <= g_display_depth) {
    std::lock_guard<std::mutex> lock(GetFileMutex());
    ::fprintf(stdout, "%*s%.9f sec (%.9f sec)\n",
              int(stack.size() - 1) * TIMER_INDENT_AMOUNT, "",
              duration<double>(total_dur).count(),
              duration<double>(timer_dur).count());
  }

  assert(stack.back() == this);
  stack.pop_back();
  if (!stack.empty())
    stack.back()->ChildDuration(total_dur);

  // Keep total results for each category so we can dump results.
  CategoryCounters &counters = g_counters[static_cast<size_t>(m_category)];
  counters.nanos += std::chrono::nanoseconds(timer_dur).count();
  counters.nanos_total += std::chrono::nanoseconds(total_dur).count();
  counters.count++;
}

void Timer::SetDisplayDepth(uint32_t depth) { g_display_depth = depth; }

void Timer::ResetCategoryTimes() {
  for (CategoryCounters &counters : g_counters)
    counters.Reset();
}

void Timer::DumpCategoryTimes(Stream &s) {
  // The categories are sorted by name.
  for (size_t i = 0; i < std::size(g_counters); ++i) {
    const CategoryCounters &counters = g_counters[i];
    uint64_t count = counters.count.load(std::memory_order_acquire);
    if (count == 0)
      continue;
    uint64_t nanos = counters.nanos.load(std::memory_order_acquire);
    uint64_t nanos_total = counters.nanos_total.load(std::memory_order_acquire);
    s.Printf("%.9f sec (total: %.3fs; child: %.3fs; count: %" PRIu64
             ") for %s\n",
             nanos / 1000000000., nanos_total / 1000000000.,
             (nanos_total - nanos) / 1000000000., count,
             g_categories[i].name.data());
  }
}

//===- LLDBTimerCategoryEmitter.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These tablegen backends emit LLDB's timer categories that are used by
// LLDB_SCOPED_TIMER (see lldb/Utility/Timer.h).
//
//===----------------------------------------------------------------------===//

#include "LLDBTableGenBackends.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <optional>
#include <vector>

using namespace llvm;
using namespace lldb_private;

namespace {
/// A timer category.
struct TimerCategory {
  StringRef Name;
  /// The dot-separated components of the timer name.
  SmallVector<StringRef, 4> Components;
};

/// A node in the tree of timer categories. Inner nodes become namespaces and
/// leaves become the CategoryID constants.
struct TreeNode {
  /// The children of this node sorted by their component name.
  std::vector<std::pair<StringRef, TreeNode>> Children;
  /// The ID of the category if this node is a leaf.
  std::optional<size_t> ID;
};
} // namespace

/// Returns true if the given string is a valid C++ identifier. Every component
/// becomes a C++ namespace or constant name.
static bool isIdentifier(StringRef Str) {
  return !Str.empty() && (isAlpha(Str.front()) || Str.front() == '_') &&
         all_of(Str, [](char C) { return isAlnum(C) || C == '_'; });
}

/// Returns all timer categories sorted by name. Their index in the result is
/// their CategoryID. Reports an error for malformed names, duplicates and
/// categories that are nested in other categories.
static std::vector<TimerCategory>
getTimerCategories(const RecordKeeper &Records) {
  // All categories sorted by their name.
  std::vector<std::pair<StringRef, const Record *>> RecordsByName;
  for (const Record *Category :
       Records.getAllDerivedDefinitions("TimerCategory"))
    RecordsByName.emplace_back(Category->getValueAsString("Name"), Category);
  if (RecordsByName.empty())
    PrintFatalError("no timer categories defined");
  // Stable sort so the earlier definition of a duplicate comes first.
  llvm::stable_sort(RecordsByName, less_first());
  for (size_t I = 1; I < RecordsByName.size(); ++I) {
    if (RecordsByName[I - 1].first != RecordsByName[I].first)
      continue;
    PrintError(RecordsByName[I].second,
               "duplicate timer category '" + RecordsByName[I].first + "'");
    PrintFatalNote(RecordsByName[I - 1].second, "previous definition is here");
  }
  auto FindRecord = [&](StringRef Name) -> const Record * {
    auto It = llvm::lower_bound(
        RecordsByName, Name,
        [](const auto &Entry, StringRef Name) { return Entry.first < Name; });
    if (It == RecordsByName.end() || It->first != Name)
      return nullptr;
    return It->second;
  };

  std::vector<TimerCategory> Result;
  for (const auto &[Name, Category] : RecordsByName) {
    TimerCategory &Entry = Result.emplace_back();
    Entry.Name = Name;
    Name.split(Entry.Components, '.');
    for (StringRef Component : Entry.Components)
      if (!isIdentifier(Component))
        PrintFatalError(Category, "timer category '" + Name +
                                      "' contains the component '" + Component +
                                      "' which is not a C++ identifier");

    // A category can't also be the parent of another category (e.g., "a.b"
    // and "a.b.c"). Otherwise "b" would be both a namespace and a constant in
    // the generated header.
    for (size_t I = 1; I < Entry.Components.size(); ++I) {
      std::string Parent = join(ArrayRef(Entry.Components).take_front(I), ".");
      const Record *ParentRecord = FindRecord(Parent);
      if (!ParentRecord)
        continue;
      PrintError(Category, "timer category '" + Name +
                               "' is nested in timer category '" + Parent +
                               "'");
      PrintFatalNote(ParentRecord,
                     "timer category '" + Parent + "' is defined here");
    }
  }
  return Result;
}

static void emitTree(const TreeNode &Node, raw_ostream &OS) {
  for (const auto &[Component, Child] : Node.Children) {
    if (Child.ID) {
      OS << "inline constexpr Timer::CategoryID " << Component << "{"
         << *Child.ID << "};\n";
      continue;
    }
    OS << "namespace " << Component << " {\n";
    emitTree(Child, OS);
    OS << "} // namespace " << Component << "\n";
  }
}

void lldb_private::EmitTimerCategories(const RecordKeeper &Records,
                                       raw_ostream &OS) {
  emitSourceFileHeader("LLDB timer categories", OS, Records);

  TreeNode Root;
  std::vector<TimerCategory> Categories = getTimerCategories(Records);
  // The categories are sorted by name and '.' sorts before all identifier
  // characters, so they are also sorted by their components. Categories that
  // share a node are therefore adjacent and each child only needs to be
  // compared against the last child of its parent.
  for (size_t ID = 0; ID < Categories.size(); ++ID) {
    TreeNode *Node = &Root;
    for (StringRef Component : Categories[ID].Components) {
      if (Node->Children.empty() || Node->Children.back().first != Component)
        Node->Children.emplace_back(Component, TreeNode());
      Node = &Node->Children.back().second;
    }
    Node->ID = ID;
  }
  emitTree(Root, OS);
}

void lldb_private::EmitTimerCategoryInfos(const RecordKeeper &Records,
                                          raw_ostream &OS) {
  emitSourceFileHeader("LLDB timer category infos", OS, Records);

  std::vector<TimerCategory> Categories = getTimerCategories(Records);
  OS << "// All timer categories indexed by their CategoryID.\n";
  OS << "static constexpr TimerCategoryInfo g_categories[] = {\n";
  for (size_t ID = 0; ID < Categories.size(); ++ID)
    OS << "    {\"" << Categories[ID].Name << "\"},\n";
  OS << "};\n";
}

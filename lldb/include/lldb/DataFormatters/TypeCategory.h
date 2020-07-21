//===-- TypeCategory.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_TYPECATEGORY_H
#define LLDB_DATAFORMATTERS_TYPECATEGORY_H

#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/FormattersContainer.h"

namespace lldb_private {

class TypeCategoryImpl {
private:
  typedef FormattersContainer<TypeFormatImpl> FormatContainer;
  typedef FormattersContainer<TypeSummaryImpl> SummaryContainer;
  typedef FormattersContainer<TypeFilterImpl> FilterContainer;
  typedef FormattersContainer<SyntheticChildren> SynthContainer;

public:
  typedef uint16_t FormatCategoryItems;
  static const uint16_t ALL_ITEM_TYPES = UINT16_MAX;

  template <typename T> class ForEachCallbacks {
  public:
    ForEachCallbacks() = default;
    ~ForEachCallbacks() = default;

    template <typename U = TypeFormatImpl>
    typename std::enable_if<std::is_same<U, T>::value, ForEachCallbacks &>::type
    Set(FormatContainer::ForEachCallback callback) {
      m_format_exact = callback;
      return *this;
    }

    template <typename U = TypeSummaryImpl>
    typename std::enable_if<std::is_same<U, T>::value, ForEachCallbacks &>::type
    Set(SummaryContainer::ForEachCallback callback) {
      m_summary_exact = callback;
      return *this;
    }

    template <typename U = TypeFilterImpl>
    typename std::enable_if<std::is_same<U, T>::value, ForEachCallbacks &>::type
    Set(FilterContainer::ForEachCallback callback) {
      m_filter_exact = callback;
      return *this;
    }

    template <typename U = SyntheticChildren>
    typename std::enable_if<std::is_same<U, T>::value, ForEachCallbacks &>::type
    Set(SynthContainer::ForEachCallback callback) {
      m_synth_exact = callback;
      return *this;
    }

    FormatContainer::ForEachCallback GetFormatExactCallback() const {
      return m_format_exact;
    }

    SummaryContainer::ForEachCallback
    GetSummaryExactCallback() const {
      return m_summary_exact;
    }

    FilterContainer::ForEachCallback GetFilterExactCallback() const {
      return m_filter_exact;
    }

    SynthContainer::ForEachCallback GetSynthExactCallback() const {
      return m_synth_exact;
    }

  private:
    FormatContainer::ForEachCallback m_format_exact;

    SummaryContainer::ForEachCallback m_summary_exact;

    FilterContainer::ForEachCallback m_filter_exact;

    SynthContainer::ForEachCallback m_synth_exact;
  };

  TypeCategoryImpl(IFormatChangeListener *clist, ConstString name);

  template <typename T> void ForEach(const ForEachCallbacks<T> &foreach) {
    GetFormatsContainer().ForEach(foreach.GetFormatExactCallback());

    GetSummariesContainer().ForEach(foreach.GetSummaryExactCallback());

    GetFiltersContainer().ForEach(foreach.GetFilterExactCallback());

    GetSyntheticsContainer().ForEach(foreach.GetSynthExactCallback());
  }

  FormatContainer &GetFormatsContainer() {
    return m_format_cont;
  }

  SummaryContainer &GetSummariesContainer() {
    return m_summary_cont;
  }

  FilterContainer &GetFiltersContainer() {
    return m_filter_cont;
  }

  FormatContainer::MapValueType
  GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp);

  SummaryContainer::MapValueType
  GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp);

  FilterContainer::MapValueType
  GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp);

  SynthContainer::MapValueType
  GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp);

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierForFormatAtIndex(size_t index);

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierForSummaryAtIndex(size_t index);

  FormatContainer::MapValueType GetFormatAtIndex(size_t index);

  SummaryContainer::MapValueType GetSummaryAtIndex(size_t index);

  FilterContainer::MapValueType GetFilterAtIndex(size_t index);

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierForFilterAtIndex(size_t index);

  SynthContainer &GetSyntheticsContainer() {
    return m_synth_cont;
  }

  SynthContainer::MapValueType GetSyntheticAtIndex(size_t index);

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierForSyntheticAtIndex(size_t index);

  bool IsEnabled() const { return m_enabled; }

  uint32_t GetEnabledPosition() {
    if (!m_enabled)
      return UINT32_MAX;
    else
      return m_enabled_position;
  }

  bool Get(lldb::LanguageType lang, const FormattersMatchVector &candidates,
           lldb::TypeFormatImplSP &entry);

  bool Get(lldb::LanguageType lang, const FormattersMatchVector &candidates,
           lldb::TypeSummaryImplSP &entry);

  bool Get(lldb::LanguageType lang, const FormattersMatchVector &candidates,
           lldb::SyntheticChildrenSP &entry);

  void Clear(FormatCategoryItems items = ALL_ITEM_TYPES);

  bool Delete(ConstString name, FormatCategoryItems items = ALL_ITEM_TYPES);

  uint32_t GetCount(FormatCategoryItems items = ALL_ITEM_TYPES);

  const char *GetName() { return m_name.GetCString(); }

  size_t GetNumLanguages();

  lldb::LanguageType GetLanguageAtIndex(size_t idx);

  void AddLanguage(lldb::LanguageType lang);

  std::string GetDescription();

  bool AnyMatches(ConstString type_name,
                  FormatCategoryItems items = ALL_ITEM_TYPES,
                  bool only_enabled = true,
                  const char **matching_category = nullptr,
                  FormatCategoryItems *matching_type = nullptr);

  typedef std::shared_ptr<TypeCategoryImpl> SharedPointer;

private:
  FormatContainer m_format_cont;
  SummaryContainer m_summary_cont;
  FilterContainer m_filter_cont;
  SynthContainer m_synth_cont;

  bool m_enabled;

  IFormatChangeListener *m_change_listener;

  std::recursive_mutex m_mutex;

  ConstString m_name;

  std::vector<lldb::LanguageType> m_languages;

  uint32_t m_enabled_position;

  void Enable(bool value, uint32_t position);

  void Disable() { Enable(false, UINT32_MAX); }

  bool IsApplicable(lldb::LanguageType lang);

  uint32_t GetLastEnabledPosition() { return m_enabled_position; }

  void SetEnabledPosition(uint32_t p) { m_enabled_position = p; }

  friend class FormatManager;
  friend class LanguageCategory;
  friend class TypeCategoryMap;

  friend class FormattersContainer<TypeFormatImpl>;

  friend class FormattersContainer<TypeSummaryImpl>;

  friend class FormattersContainer<TypeFilterImpl>;

  friend class FormattersContainer<ScriptedSyntheticChildren>;
};

} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_TYPECATEGORY_H

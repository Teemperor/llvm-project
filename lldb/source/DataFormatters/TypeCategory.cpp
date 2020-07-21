//===-- TypeCategory.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/TypeCategory.h"
#include "lldb/Target/Language.h"


using namespace lldb;
using namespace lldb_private;

TypeCategoryImpl::TypeCategoryImpl(IFormatChangeListener *clist,
                                   ConstString name)
    : m_format_cont(clist), m_summary_cont(clist), m_filter_cont(clist),
      m_synth_cont(clist), m_enabled(false), m_change_listener(clist),
      m_mutex(), m_name(name), m_languages() {}

static bool IsApplicable(lldb::LanguageType category_lang,
                         lldb::LanguageType valobj_lang) {
  switch (category_lang) {
  // Unless we know better, allow only exact equality.
  default:
    return category_lang == valobj_lang;

  // the C family, we consider it as one
  case eLanguageTypeC89:
  case eLanguageTypeC:
  case eLanguageTypeC99:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99;

  // ObjC knows about C and itself
  case eLanguageTypeObjC:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 || valobj_lang == eLanguageTypeObjC;

  // C++ knows about C and C++
  case eLanguageTypeC_plus_plus:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 ||
           valobj_lang == eLanguageTypeC_plus_plus;

  // ObjC++ knows about C,C++,ObjC and ObjC++
  case eLanguageTypeObjC_plus_plus:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 ||
           valobj_lang == eLanguageTypeC_plus_plus ||
           valobj_lang == eLanguageTypeObjC;

  // Categories with unspecified language match everything.
  case eLanguageTypeUnknown:
    return true;
  }
}

bool TypeCategoryImpl::IsApplicable(lldb::LanguageType lang) {
  for (size_t idx = 0; idx < GetNumLanguages(); idx++) {
    const lldb::LanguageType category_lang = GetLanguageAtIndex(idx);
    if (::IsApplicable(category_lang, lang))
      return true;
  }
  return false;
}

size_t TypeCategoryImpl::GetNumLanguages() {
  if (m_languages.empty())
    return 1;
  return m_languages.size();
}

lldb::LanguageType TypeCategoryImpl::GetLanguageAtIndex(size_t idx) {
  if (m_languages.empty())
    return lldb::eLanguageTypeUnknown;
  return m_languages[idx];
}

void TypeCategoryImpl::AddLanguage(lldb::LanguageType lang) {
  m_languages.push_back(lang);
}

bool TypeCategoryImpl::Get(lldb::LanguageType lang,
                           const FormattersMatchVector &candidates,
                           lldb::TypeFormatImplSP &entry) {
  if (!IsEnabled() || !IsApplicable(lang))
    return false;
  return GetFormatsContainer().Get(candidates, entry);
}

bool TypeCategoryImpl::Get(lldb::LanguageType lang,
                           const FormattersMatchVector &candidates,
                           lldb::TypeSummaryImplSP &entry) {
  if (!IsEnabled() || !IsApplicable(lang))
    return false;
  return GetSummariesContainer().Get(candidates, entry);
}

bool TypeCategoryImpl::Get(lldb::LanguageType lang,
                           const FormattersMatchVector &candidates,
                           lldb::SyntheticChildrenSP &entry) {
  if (!IsEnabled() || !IsApplicable(lang))
    return false;
  TypeFilterImpl::SharedPointer filter_sp;
  // first find both Filter and Synth, and then check which is most recent

  GetFiltersContainer().Get(candidates, filter_sp);

  bool pick_synth = false;
  ScriptedSyntheticChildren::SharedPointer synth;
  GetSyntheticsContainer().Get(candidates, synth);
  if (!filter_sp.get() && !synth.get())
    return false;
  else if (!filter_sp.get() && synth.get())
    pick_synth = true;

  else if (filter_sp.get() && !synth.get())
    pick_synth = false;

  else /*if (filter_sp.get() && synth.get())*/
  {
    pick_synth = filter_sp->GetRevision() <= synth->GetRevision();
  }
  if (pick_synth) {
    entry = synth;
    return true;
  } else {
    entry = filter_sp;
    return true;
  }
  return false;
}

void TypeCategoryImpl::Clear(FormatCategoryItems items) {
  if ((items & eFormatCategoryItemValue) == eFormatCategoryItemValue)
    GetFormatsContainer().Clear();

  if ((items & eFormatCategoryItemSummary) == eFormatCategoryItemSummary)
    GetSummariesContainer().Clear();

  if ((items & eFormatCategoryItemFilter) == eFormatCategoryItemFilter)
    GetFiltersContainer().Clear();

  if ((items & eFormatCategoryItemSynth) == eFormatCategoryItemSynth)
    GetSyntheticsContainer().Clear();
}

bool TypeCategoryImpl::Delete(TypeMatcher name, FormatCategoryItems items) {
  bool success = false;

  if ((items & eFormatCategoryItemValue) == eFormatCategoryItemValue)
    success = GetFormatsContainer().Delete(name) || success;

  if ((items & eFormatCategoryItemSummary) == eFormatCategoryItemSummary)
    success = GetSummariesContainer().Delete(name) || success;

  if ((items & eFormatCategoryItemFilter) == eFormatCategoryItemFilter)
    success = GetFiltersContainer().Delete(name) || success;

  if ((items & eFormatCategoryItemSynth) == eFormatCategoryItemSynth)
    success = GetSyntheticsContainer().Delete(name) || success;

  return success;
}

uint32_t TypeCategoryImpl::GetCount(FormatCategoryItems items) {
  uint32_t count = 0;

  if ((items & eFormatCategoryItemValue) == eFormatCategoryItemValue)
    count += GetFormatsContainer().GetCount();

  if ((items & eFormatCategoryItemSummary) == eFormatCategoryItemSummary)
    count += GetSummariesContainer().GetCount();

  if ((items & eFormatCategoryItemFilter) == eFormatCategoryItemFilter)
    count += GetFiltersContainer().GetCount();

  if ((items & eFormatCategoryItemSynth) == eFormatCategoryItemSynth)
    count += GetSyntheticsContainer().GetCount();

  return count;
}

bool TypeCategoryImpl::AnyMatches(ConstString type_name,
                                  FormatCategoryItems items, bool only_enabled,
                                  const char **matching_category,
                                  FormatCategoryItems *matching_type) {
  if (!IsEnabled() && only_enabled)
    return false;

  lldb::TypeFormatImplSP format_sp;
  lldb::TypeSummaryImplSP summary_sp;
  TypeFilterImpl::SharedPointer filter_sp;
  ScriptedSyntheticChildren::SharedPointer synth_sp;

  if ((items & eFormatCategoryItemValue) == eFormatCategoryItemValue) {
    if (GetFormatsContainer().Get(type_name, format_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemValue;
      return true;
    }
  }

  if ((items & eFormatCategoryItemSummary) == eFormatCategoryItemSummary) {
    if (GetSummariesContainer().Get(type_name, summary_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemSummary;
      return true;
    }
  }

  if ((items & eFormatCategoryItemFilter) == eFormatCategoryItemFilter) {
    if (GetFiltersContainer().Get(type_name, filter_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemFilter;
      return true;
    }
  }

  if ((items & eFormatCategoryItemSynth) == eFormatCategoryItemSynth) {
    if (GetSyntheticsContainer().Get(type_name, synth_sp)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemSynth;
      return true;
    }
  }

  return false;
}

TypeCategoryImpl::FormatContainer::ValueSP
TypeCategoryImpl::GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp) {
  FormatContainer::ValueSP retval;

  if (type_sp) {
      GetFormatsContainer().GetExact(ConstString(type_sp->GetName()),
                                          retval);
  }

  return retval;
}

TypeCategoryImpl::SummaryContainer::ValueSP
TypeCategoryImpl::GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp) {
  SummaryContainer::ValueSP retval;

  if (type_sp) {
      GetSummariesContainer().GetExact(ConstString(type_sp->GetName()),
                                            retval);
  }

  return retval;
}

TypeCategoryImpl::FilterContainer::ValueSP
TypeCategoryImpl::GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp) {
  FilterContainer::ValueSP retval;

  if (type_sp) {
      GetFiltersContainer().GetExact(ConstString(type_sp->GetName()),
                                          retval);
  }

  return retval;
}

TypeCategoryImpl::SynthContainer::ValueSP
TypeCategoryImpl::GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp) {
  SynthContainer::ValueSP retval;

  if (type_sp) {
      GetSyntheticsContainer().GetExact(ConstString(type_sp->GetName()),
                                             retval);
  }

  return retval;
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForSummaryAtIndex(size_t index) {
  return GetSummariesContainer().GetTypeNameSpecifierAtIndex(index);
}

TypeCategoryImpl::FormatContainer::ValueSP
TypeCategoryImpl::GetFormatAtIndex(size_t index) {
  return GetFormatsContainer().GetAtIndex(index);
}

TypeCategoryImpl::SummaryContainer::ValueSP
TypeCategoryImpl::GetSummaryAtIndex(size_t index) {
  return GetSummariesContainer().GetAtIndex(index);
}

TypeCategoryImpl::FilterContainer::ValueSP
TypeCategoryImpl::GetFilterAtIndex(size_t index) {
  return GetFiltersContainer().GetAtIndex(index);
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForFormatAtIndex(size_t index) {
  return GetFormatsContainer().GetTypeNameSpecifierAtIndex(index);
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForFilterAtIndex(size_t index) {
  return GetFiltersContainer().GetTypeNameSpecifierAtIndex(index);
}

TypeCategoryImpl::SynthContainer::ValueSP
TypeCategoryImpl::GetSyntheticAtIndex(size_t index) {
  return GetSyntheticsContainer().GetAtIndex(index);
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForSyntheticAtIndex(size_t index) {
  return GetSyntheticsContainer().GetTypeNameSpecifierAtIndex(index);
}

void TypeCategoryImpl::Enable(bool value, uint32_t position) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if ((m_enabled = value))
    m_enabled_position = position;
  if (m_change_listener)
    m_change_listener->Changed();
}

std::string TypeCategoryImpl::GetDescription() {
  StreamString stream;
  stream.Printf("%s (%s", GetName(), (IsEnabled() ? "enabled" : "disabled"));
  StreamString lang_stream;
  lang_stream.Printf(", applicable for language(s): ");
  bool print_lang = false;
  for (size_t idx = 0; idx < GetNumLanguages(); idx++) {
    const lldb::LanguageType lang = GetLanguageAtIndex(idx);
    if (lang != lldb::eLanguageTypeUnknown)
      print_lang = true;
    lang_stream.Printf("%s%s", Language::GetNameForLanguageType(lang),
                       idx + 1 < GetNumLanguages() ? ", " : "");
  }
  if (print_lang)
    stream.PutCString(lang_stream.GetString());
  stream.PutChar(')');
  return std::string(stream.GetString());
}

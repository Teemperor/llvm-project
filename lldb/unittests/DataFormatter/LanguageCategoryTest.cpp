//===-- LanguageCategory.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/DataFormatters/LanguageCategory.h"
#include "gtest/gtest.h"
#include "TestingSupport/SubsystemRAII.h"
#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"

using namespace lldb;
using namespace lldb_private;

struct DummyValueObject : public ValueObject {
  virtual llvm::Optional<uint64_t> GetByteSize() {
    return llvm::None;
  }

  virtual lldb::ValueType GetValueType() const {
    return lldb::ValueType::eValueTypeInvalid;
  }

  virtual bool UpdateValue() {
    return true;
  }

  virtual size_t CalculateNumChildren(uint32_t max = UINT32_MAX) {
    return 0;
  }

  virtual CompilerType GetCompilerTypeImpl() {
    return CompilerType();
  }
};

TEST(LanguageCategory, asdf) {
  SubsystemRAII<CPlusPlusLanguage> subsystems;

  FormatManager manager;
  LanguageCategory bla(lldb::LanguageType::eLanguageTypeC_plus_plus);

  DummyValueObject obj;

  FormattersMatchData match_data(obj, lldb::DynamicValueType::eNoDynamicValues);
  lldb::SyntheticChildrenSP impl;
  bla.GetHardcoded(manager, match_data, impl);
}

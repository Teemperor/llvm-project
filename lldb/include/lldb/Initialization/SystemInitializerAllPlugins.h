//===-- SystemInitializerAllPlugins.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SYSTEM_INITIALIZER_ALL_PLUGINS_H
#define LLDB_API_SYSTEM_INITIALIZER_ALL_PLUGINS_H

#include "lldb/Initialization/SystemInitializerCommon.h"

namespace lldb_private {

/// Initializes all LLDB plugins.
class SystemInitializerAllPlugins : public SystemInitializerCommon {
public:
  SystemInitializerAllPlugins();
  ~SystemInitializerAllPlugins() override;

  llvm::Error Initialize() override;
  void Terminate() override;
};

} // namespace lldb_private

#endif // LLDB_API_SYSTEM_INITIALIZER_ALL_PLUGINS_H

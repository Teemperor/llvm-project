//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ScratchTypeSystemClike.h"

#include "lldb/Target/Target.h"

using namespace lldb_private;
using namespace lldb;

char ScratchTypeSystemClike::ID;

ScratchTypeSystemClike::ScratchTypeSystemClike(Target &target,
                                               clike::LanguageOpts opts)
    : TypeSystemClike(std::string("scratch TypeSystemClike for ") +
                          target.GetArchitecture().GetArchitectureName(),
                      std::move(opts)) {}

ScratchTypeSystemClike::~ScratchTypeSystemClike() = default;

llvm::Expected<lldb::TypeSystemSP>
ScratchTypeSystemClike::Create(Target &target, llvm::Triple triple) {
  llvm::Expected<clike::LanguageOpts> opts =
      clike::LanguageOpts::Create(std::move(triple));
  if (!opts)
    return opts.takeError();
  return std::make_shared<ScratchTypeSystemClike>(target, std::move(*opts));
}

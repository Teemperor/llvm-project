//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ScratchTypeSystemClike.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"

#include "lldb/Target/Target.h"

using namespace lldb_private;
using namespace lldb;

char ScratchTypeSystemClike::ID;

ScratchTypeSystemClike::ScratchTypeSystemClike(Target &target,
                                               clike::LanguageOpts opts)
    : TypeSystemClike(std::string("scratch TypeSystemClike for ") +
                          target.GetArchitecture().GetArchitectureName(),
                      std::move(opts)),
      m_target_wp(target.shared_from_this()) {
  // See TypeSystemClike::GetScratchInstances: module instances have to be able
  // to find us before they take any lock.
  RegisterScratchInstance(this);
}

ScratchTypeSystemClike::~ScratchTypeSystemClike() {
  UnregisterScratchInstance(this);
}

llvm::Expected<lldb::TypeSystemSP>
ScratchTypeSystemClike::Create(Target &target, llvm::Triple triple) {
  llvm::Expected<clike::LanguageOpts> opts =
      clike::LanguageOpts::Create(std::move(triple));
  if (!opts)
    return opts.takeError();
  return std::make_shared<ScratchTypeSystemClike>(target, std::move(*opts));
}

void ScratchTypeSystemClike::AppendLockOrder(
    llvm::SmallVectorImpl<TypeSystemClike *> &out) const {
  TypeSystemClike::AppendLockOrder(out);
  lldb::TargetSP target_sp = m_target_wp.lock();
  if (!target_sp)
    return;
  // Copy the pointers out under the image list's own lock and release it before
  // returning: GetLockOrder's caller is about to take TypeSystemClike locks,
  // and holding the module list across that would introduce a second lock order
  // to reason about.
  for (const lldb::ModuleSP &module_sp : target_sp->GetImages().Modules())
    if (module_sp)
      AppendClikeTypeSystemsOf(*module_sp, out);
}

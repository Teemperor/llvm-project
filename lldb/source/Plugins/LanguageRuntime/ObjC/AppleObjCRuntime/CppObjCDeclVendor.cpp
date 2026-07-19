//===-- CppObjCDeclVendor.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CppObjCDeclVendor.h"

#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb_private;

CppObjCDeclVendor::CppObjCDeclVendor(ObjCLanguageRuntime &runtime)
    : DeclVendor(eCppObjCDeclVendor), m_runtime(runtime) {}

uint32_t CppObjCDeclVendor::FindDecls(ConstString name, bool append,
                                     uint32_t max_matches,
                                     std::vector<CompilerDecl> &decls) {
  // Deliberately always empty -- see the class comment in the header for why
  // this is load-bearing (ObjCLanguageRuntime::LookupInRuntime()) and not
  // just "unimplemented".
  if (!append)
    decls.clear();
  return 0;
}

std::vector<CompilerType>
CppObjCDeclVendor::FindTypes(ConstString name, uint32_t max_matches) {
  Process *process = m_runtime.GetProcess();
  if (!process)
    return {};

  llvm::Expected<lldb::TypeSystemSP> scratch_or =
      process->GetTarget().GetScratchTypeSystemForLanguage(
          lldb::eLanguageTypeObjC_plus_plus);
  if (!scratch_or) {
    llvm::consumeError(scratch_or.takeError());
    return {};
  }

  auto *scratch = llvm::dyn_cast_or_null<TypeSystemCpp>(scratch_or->get());
  if (!scratch)
    return {};

  CompilerType type =
      scratch->CreateRuntimeObjCInterface(name, *process, m_runtime);
  if (!type)
    return {};

  return {type};
}

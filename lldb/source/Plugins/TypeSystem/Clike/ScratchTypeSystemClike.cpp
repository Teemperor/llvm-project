//===-- ScratchTypeSystemClike.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ScratchTypeSystemClike.h"

#include "Plugins/ExpressionParser/Clang/ClangFunctionCaller.h"
#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "Plugins/ExpressionParser/Clang/ClangUserExpression.h"
#include "Plugins/ExpressionParser/Clang/ClangUtilityFunction.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb_private;
using namespace lldb;

char ScratchTypeSystemClike::ID;

ScratchTypeSystemClike::ScratchTypeSystemClike(Target &target, llvm::Triple triple)
    : TypeSystemClike(std::string("scratch TypeSystemClike for ") +
                        target.GetArchitecture().GetArchitectureName(),
                    std::move(triple)),
      m_target_wp(target.shared_from_this()) {}

UserExpression *ScratchTypeSystemClike::GetUserExpression(
    llvm::StringRef expr, llvm::StringRef prefix, SourceLanguage language,
    Expression::ResultType desired_type,
    const EvaluateExpressionOptions &options, ValueObject *ctx_obj) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return nullptr;
  return new ClangUserExpression(*target, expr, prefix, language, desired_type,
                                 options, ctx_obj);
}

FunctionCaller *ScratchTypeSystemClike::GetFunctionCaller(
    const CompilerType &return_type, const Address &function_address,
    const ValueList &arg_value_list, const char *name) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return nullptr;
  Process *process = target->GetProcessSP().get();
  if (!process)
    return nullptr;
  return new ClangFunctionCaller(*process, return_type, function_address,
                                 arg_value_list, name);
}

std::unique_ptr<UtilityFunction>
ScratchTypeSystemClike::CreateUtilityFunction(std::string text,
                                            std::string name) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return {};
  return std::make_unique<ClangUtilityFunction>(
      *target, std::move(text), std::move(name),
      target->GetDebugUtilityExpression());
}

PersistentExpressionState *
ScratchTypeSystemClike::GetPersistentExpressionState() {
  if (!m_persistent_variables) {
    TargetSP target = m_target_wp.lock();
    if (!target)
      return nullptr;
    m_persistent_variables =
        std::make_unique<ClangPersistentVariables>(target->shared_from_this());
  }
  return m_persistent_variables.get();
}

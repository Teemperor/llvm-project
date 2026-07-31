//===-- ScratchTypeSystemClike.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCRATCHTYPESYSTEMCLIKE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCRATCHTYPESYSTEMCLIKE_H

#include "TypeSystemClike.h"

#include "lldb/Expression/Expression.h"
#include "lldb/lldb-forward.h"

#include <memory>
#include <string>

namespace lldb_private {

class Address;
class EvaluateExpressionOptions;
class PersistentExpressionState;
class Target;
class UtilityFunction;
class ValueList;

/// The per-Target TypeSystemClike: the one that owns types that aren't tied to
/// any single module (expression results, persistent variables, types
/// reconstructed from the Objective-C runtime), and the one that answers the
/// expression-evaluation entry points a Target needs.
///
/// It is created by TypeSystemClang::CreateInstance when
/// `symbols.enable-typesystem-clike` is on, which is how TypeSystemClike
/// piggybacks on the Clang plugin's language registration.
class ScratchTypeSystemClike : public TypeSystemClike {
  static char ID;

public:
  ScratchTypeSystemClike(Target &target, llvm::Triple triple);

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || TypeSystemClike::isA(ClassID);
  }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  // Expressions are still parsed by the Clang expression parser (which builds a
  // transient clang::ASTContext); the TypeSystemClike-specific work -- translating
  // debug-info types into that Clang AST and mapping the result type back onto a
  // TypeSystemClike type -- is handled by ClikeExpressionDeclMap, which the parser
  // installs when this setting is on. No scratch TypeSystemClang is involved.
  UserExpression *
  GetUserExpression(llvm::StringRef expr, llvm::StringRef prefix,
                    SourceLanguage language, Expression::ResultType desired_type,
                    const EvaluateExpressionOptions &options,
                    ValueObject *ctx_obj) override;

  FunctionCaller *GetFunctionCaller(const CompilerType &return_type,
                                    const Address &function_address,
                                    const ValueList &arg_value_list,
                                    const char *name) override;

  std::unique_ptr<UtilityFunction>
  CreateUtilityFunction(std::string text, std::string name) override;

  PersistentExpressionState *GetPersistentExpressionState() override;

private:
  lldb::TargetWP m_target_wp;
  /// Persistent variables ($0, $foo, ...) for expressions evaluated in this
  /// scratch context. Created lazily.
  std::unique_ptr<PersistentExpressionState> m_persistent_variables;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCRATCHTYPESYSTEMCLIKE_H

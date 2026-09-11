//===----------------------------------------------------------------------===//
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
#include <vector>

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
/// `symbols.experimental.enable-typesystem-clike` is on, which is how
/// TypeSystemClike piggybacks on the Clang plugin's language registration.
class ScratchTypeSystemClike : public TypeSystemClike {
  static char ID;

public:
  /// \p opts must already have been derived from a real target; use Create
  /// below rather than deriving them at the call site.
  ScratchTypeSystemClike(Target &target, clike::LanguageOpts opts);
  ~ScratchTypeSystemClike() override;

  /// The scratch type system for \p target, or an error if language options
  /// cannot be derived for \p triple (see
  /// clike::LanguageOpts::Create).
  static llvm::Expected<lldb::TypeSystemSP> Create(Target &target,
                                                   llvm::Triple triple);

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || TypeSystemClike::isA(ClassID);
  }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  // Expressions are still parsed by the Clang expression parser (which builds a
  // transient clang::ASTContext); the TypeSystemClike-specific work --
  // translating debug-info types into that Clang AST and mapping the result
  // type back onto a TypeSystemClike type -- is handled by
  // ClikeExpressionDeclMap, which the parser installs when this setting is on.
  // No scratch TypeSystemClang is involved.
  UserExpression *GetUserExpression(llvm::StringRef expr,
                                    llvm::StringRef prefix,
                                    SourceLanguage language,
                                    Expression::ResultType desired_type,
                                    const EvaluateExpressionOptions &options,
                                    ValueObject *ctx_obj) override;

  FunctionCaller *GetFunctionCaller(const CompilerType &return_type,
                                    const Address &function_address,
                                    const ValueList &arg_value_list,
                                    const char *name) override;

  std::unique_ptr<UtilityFunction>
  CreateUtilityFunction(std::string text, std::string name) override;

  PersistentExpressionState *GetPersistentExpressionState() override;

protected:
  /// The scratch instance holds the types the expression evaluator and the data
  /// formatters build, and those are routinely assembled *around* types the
  /// module that parsed them still owns -- a reconstructed `T *` whose pointee
  /// belongs to a module, an elaborated spelling over a module's typedef (see
  /// ClangTypeConverter). It also reaches into module instances by control flow
  /// alone, with no reference behind it at all: a module answering a child
  /// query about an Objective-C interface calls in here to have it rebuilt from
  /// the runtime (see TypeSystemClike::GetRuntimeCompletedObjCType). So rather
  /// than try to enumerate which modules those are, take the lot: every module
  /// in the target, plus whatever the base contributes.
  void
  AppendLockOrder(llvm::SmallVectorImpl<TypeSystemClike *> &out) const override;

  /// The target's image list grows as libraries are loaded, and an image can
  /// join it with a TypeSystemClike that already exists (a module shared with
  /// another target, or kept alive by the shared module cache), which the
  /// global lock-order epoch would not notice. So compare the images against
  /// the snapshot AppendLockOrder took, and bump the epoch when they differ:
  /// module instances fold this set into theirs.
  bool IsLockOrderStale() const override;

private:
  lldb::TargetWP m_target_wp;
  /// The target's images as of the last AppendLockOrder. Guarded by
  /// m_lock_order_mutex (only touched from AppendLockOrder/IsLockOrderStale).
  mutable std::vector<Module *> m_lock_order_images;
  /// Persistent variables ($0, $foo, ...) for expressions evaluated in this
  /// scratch context. Created lazily.
  std::unique_ptr<PersistentExpressionState> m_persistent_variables;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCRATCHTYPESYSTEMCLIKE_H

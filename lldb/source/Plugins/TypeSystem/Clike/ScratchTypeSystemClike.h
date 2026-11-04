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

#include "lldb/lldb-forward.h"

#include <memory>
#include <string>

namespace lldb_private {

class Target;

/// The per-Target TypeSystemClike: the one that owns types that aren't tied to
/// any single module (e.g. types reconstructed from the Objective-C runtime).
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

protected:
  /// The scratch instance holds the types the expression evaluator and the data
  /// formatters build, and those are routinely assembled *around* types the
  /// module that parsed them still owns -- a reconstructed `T *` whose pointee
  /// belongs to a module, an elaborated spelling over a module's typedef (see
  /// ClangTypeConverter). So rather than try to enumerate which modules those
  /// are, take the lot: every module in the target, plus whatever the base
  /// contributes.
  void
  AppendLockOrder(llvm::SmallVectorImpl<TypeSystemClike *> &out) const override;

  /// The target's image list grows as libraries are loaded, so unlike a module
  /// instance the scratch cannot compute its set once.
  bool HasDynamicLockOrder() const override { return true; }

private:
  lldb::TargetWP m_target_wp;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCRATCHTYPESYSTEMCLIKE_H

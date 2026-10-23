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
#include <vector>

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
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCRATCHTYPESYSTEMCLIKE_H

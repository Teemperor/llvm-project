//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Namespace.h"

#include "llvm/Support/Casting.h"

using namespace lldb_private;
using namespace lldb_private::clike;

const Namespace *clike::GetEnclosingNamespace(const DeclarationContext *ctx) {
  for (; ctx; ctx = ctx->GetParent())
    if (const auto *ns = llvm::dyn_cast<Namespace>(ctx))
      return ns;
  return nullptr;
}

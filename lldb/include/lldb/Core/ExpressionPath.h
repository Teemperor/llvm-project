//===-- ExpressionPath.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_EXPRESSIONPATH_H
#define LLDB_CORE_EXPRESSIONPATH_H

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

/// Responsible for evaluating LLDB's expression paths which is the
/// domain-specific language of 'frame var' or SBValue::GetExpressionPath.
class ExpressionPath {
  // Right now the parser has no state so there is no need to construct one.
  ExpressionPath() = default;

public:
  enum class PathFormat { DereferencePointers = 1, HonorPointers };

  enum class ScanEndReason {
    /// Out of data to parse.
    EndOfString = 1,
    /// Child element not found.
    NoSuchChild,
    /// (Synthetic) child  element not found.
    NoSuchSyntheticChild,
    /// [] only allowed for arrays.
    EmptyRangeNotAllowed,
    /// . used when -> should be used.
    DotInsteadOfArrow,
    /// -> used when . should be used.
    ArrowInsteadOfDot,
    /// ObjC ivar expansion not allowed.
    FragileIVarNotAllowed,
    /// [] not allowed by options.
    RangeOperatorNotAllowed,
    /// [] not valid on objects  other than scalars, pointers or arrays.
    RangeOperatorInvalid,
    /// [] is good for arrays,  but I cannot parse it.
    ArrayRangeOperatorMet,
    /// [] is good for bitfields, but I cannot parse after it.
    BitfieldRangeOperatorMet,
    /// Something is malformed in he expression.
    UnexpectedSymbol,
    /// Impossible to apply &  operator.
    TakingAddressFailed,
    /// Impossible to apply *  operator.
    DereferencingFailed,
    /// [] was expanded into a  VOList.
    RangeOperatorExpanded,
    /// getting the synthetic children failed.
    SyntheticValueMissing,
    Unknown = 0xFFFF
  };

  enum class EndResultType {
    /// Anything but...
    Plain = 1,
    /// A bitfield.
    Bitfield,
    /// A range [low-high].
    BoundedRange,
    /// A range [].
    UnboundedRange,
    /// Several items in a VOList.
    ValueObjectList,
    Invalid = 0xFFFF
  };

  enum class Aftermath {
    /// Just return it.
    Nothing = 1,
    /// Dereference the target.
    Dereference,
    /// Take target's address.
    TakeAddress
  };

  struct GetValueOptions {
    enum class SyntheticChildrenTraversal {
      None,
      ToSynthetic,
      FromSynthetic,
      Both
    };

    bool m_check_dot_vs_arrow_syntax;
    bool m_no_fragile_ivar;
    bool m_allow_bitfields_syntax;
    SyntheticChildrenTraversal m_synthetic_children_traversal;

    GetValueOptions(bool dot = false, bool no_ivar = false,
                    bool bitfield = true,
                    SyntheticChildrenTraversal synth_traverse =
                        SyntheticChildrenTraversal::ToSynthetic)
        : m_check_dot_vs_arrow_syntax(dot), m_no_fragile_ivar(no_ivar),
          m_allow_bitfields_syntax(bitfield),
          m_synthetic_children_traversal(synth_traverse) {}

    GetValueOptions &DoCheckDotVsArrowSyntax() {
      m_check_dot_vs_arrow_syntax = true;
      return *this;
    }

    GetValueOptions &DontCheckDotVsArrowSyntax() {
      m_check_dot_vs_arrow_syntax = false;
      return *this;
    }

    GetValueOptions &DoAllowFragileIVar() {
      m_no_fragile_ivar = false;
      return *this;
    }

    GetValueOptions &DontAllowFragileIVar() {
      m_no_fragile_ivar = true;
      return *this;
    }

    GetValueOptions &DoAllowBitfieldSyntax() {
      m_allow_bitfields_syntax = true;
      return *this;
    }

    GetValueOptions &DontAllowBitfieldSyntax() {
      m_allow_bitfields_syntax = false;
      return *this;
    }

    GetValueOptions &
    SetSyntheticChildrenTraversal(SyntheticChildrenTraversal traverse) {
      m_synthetic_children_traversal = traverse;
      return *this;
    }

    static const GetValueOptions DefaultOptions() {
      static GetValueOptions g_default_options;

      return g_default_options;
    }
  };

  static lldb::ValueObjectSP
  Parse(lldb::ValueObjectSP root, llvm::StringRef expression,
        ScanEndReason *reason_to_stop, EndResultType *final_result,
        const GetValueOptions &options, Aftermath *what_next);
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECT_H

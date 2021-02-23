//===-- ExpressionPath.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ExpressionPath.h"
#include "lldb/Core/ValueObject.h"

using namespace lldb;
using namespace lldb_private;

ValueObjectSP ExpressionPath::Parse(ValueObjectSP root,
                                    llvm::StringRef expression,
                                    ScanEndReason *reason_to_stop,
                                    EndResultType *final_result,
                                    const GetValueOptions &options,
                                    Aftermath *what_next) {

  if (!root)
    return nullptr;

  llvm::StringRef remainder = expression;

  while (true) {
    llvm::StringRef temp_expression = remainder;

    CompilerType root_compiler_type = root->GetCompilerType();
    CompilerType pointee_compiler_type;
    Flags pointee_compiler_type_info;

    Flags root_compiler_type_info(
        root_compiler_type.GetTypeInfo(&pointee_compiler_type));
    if (pointee_compiler_type)
      pointee_compiler_type_info.Reset(pointee_compiler_type.GetTypeInfo());

    if (temp_expression.empty()) {
      *reason_to_stop = ScanEndReason::EndOfString;
      return root;
    }

    switch (temp_expression.front()) {
    case '-': {
      temp_expression = temp_expression.drop_front();
      if (options.m_check_dot_vs_arrow_syntax &&
          root_compiler_type_info.Test(eTypeIsPointer)) // if you are trying to
                                                        // use -> on a
                                                        // non-pointer and I
                                                        // must catch the error
      {
        *reason_to_stop = ScanEndReason::ArrowInsteadOfDot;
        *final_result = EndResultType::Invalid;
        return ValueObjectSP();
      }
      if (root_compiler_type_info.Test(eTypeIsObjC) && // if yo are trying to
                                                       // extract an ObjC IVar
                                                       // when this is forbidden
          root_compiler_type_info.Test(eTypeIsPointer) &&
          options.m_no_fragile_ivar) {
        *reason_to_stop = ScanEndReason::FragileIVarNotAllowed;
        *final_result = EndResultType::Invalid;
        return ValueObjectSP();
      }
      if (!temp_expression.startswith(">")) {
        *reason_to_stop = ScanEndReason::UnexpectedSymbol;
        *final_result = EndResultType::Invalid;
        return ValueObjectSP();
      }
    }
      LLVM_FALLTHROUGH;
    case '.': // or fallthrough from ->
    {
      if (options.m_check_dot_vs_arrow_syntax &&
          temp_expression.front() == '.' &&
          root_compiler_type_info.Test(eTypeIsPointer)) // if you are trying to
                                                        // use . on a pointer
                                                        // and I must catch the
                                                        // error
      {
        *reason_to_stop = ScanEndReason::DotInsteadOfArrow;
        *final_result = EndResultType::Invalid;
        return nullptr;
      }
      temp_expression = temp_expression.drop_front(); // skip . or >

      size_t next_sep_pos = temp_expression.find_first_of("-.[", 1);
      ConstString child_name;
      if (next_sep_pos == llvm::StringRef::npos) // if no other separator just
                                                 // expand this last layer
      {
        child_name.SetString(temp_expression);
        ValueObjectSP child_valobj_sp =
            root->GetChildMemberWithName(child_name, true);

        if (child_valobj_sp.get()) // we know we are done, so just return
        {
          *reason_to_stop = ScanEndReason::EndOfString;
          *final_result = EndResultType::Plain;
          return child_valobj_sp;
        } else {
          switch (options.m_synthetic_children_traversal) {
          case GetValueOptions::SyntheticChildrenTraversal::None:
            break;
          case GetValueOptions::SyntheticChildrenTraversal::FromSynthetic:
            if (root->IsSynthetic()) {
              child_valobj_sp = root->GetNonSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          case GetValueOptions::SyntheticChildrenTraversal::ToSynthetic:
            if (!root->IsSynthetic()) {
              child_valobj_sp = root->GetSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          case GetValueOptions::SyntheticChildrenTraversal::Both:
            if (root->IsSynthetic()) {
              child_valobj_sp = root->GetNonSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            } else {
              child_valobj_sp = root->GetSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          }
        }

        // if we are here and options.m_no_synthetic_children is true,
        // child_valobj_sp is going to be a NULL SP, so we hit the "else"
        // branch, and return an error
        if (child_valobj_sp.get()) // if it worked, just return
        {
          *reason_to_stop = ScanEndReason::EndOfString;
          *final_result = EndResultType::Plain;
          return child_valobj_sp;
        } else {
          *reason_to_stop = ScanEndReason::NoSuchChild;
          *final_result = EndResultType::Invalid;
          return nullptr;
        }
      } else // other layers do expand
      {
        llvm::StringRef next_separator = temp_expression.substr(next_sep_pos);

        child_name.SetString(temp_expression.slice(0, next_sep_pos));

        ValueObjectSP child_valobj_sp =
            root->GetChildMemberWithName(child_name, true);
        if (child_valobj_sp.get()) // store the new root and move on
        {
          root = child_valobj_sp;
          remainder = next_separator;
          *final_result = EndResultType::Plain;
          continue;
        } else {
          switch (options.m_synthetic_children_traversal) {
          case GetValueOptions::SyntheticChildrenTraversal::None:
            break;
          case GetValueOptions::SyntheticChildrenTraversal::FromSynthetic:
            if (root->IsSynthetic()) {
              child_valobj_sp = root->GetNonSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          case GetValueOptions::SyntheticChildrenTraversal::ToSynthetic:
            if (!root->IsSynthetic()) {
              child_valobj_sp = root->GetSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          case GetValueOptions::SyntheticChildrenTraversal::Both:
            if (root->IsSynthetic()) {
              child_valobj_sp = root->GetNonSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            } else {
              child_valobj_sp = root->GetSyntheticValue();
              if (child_valobj_sp.get())
                child_valobj_sp =
                    child_valobj_sp->GetChildMemberWithName(child_name, true);
            }
            break;
          }
        }

        // if we are here and options.m_no_synthetic_children is true,
        // child_valobj_sp is going to be a NULL SP, so we hit the "else"
        // branch, and return an error
        if (child_valobj_sp.get()) // if it worked, move on
        {
          root = child_valobj_sp;
          remainder = next_separator;
          *final_result = EndResultType::Plain;
          continue;
        } else {
          *reason_to_stop = ScanEndReason::NoSuchChild;
          *final_result = EndResultType::Invalid;
          return nullptr;
        }
      }
      break;
    }
    case '[': {
      if (!root_compiler_type_info.Test(eTypeIsArray) &&
          !root_compiler_type_info.Test(eTypeIsPointer) &&
          !root_compiler_type_info.Test(
              eTypeIsVector)) // if this is not a T[] nor a T*
      {
        if (!root_compiler_type_info.Test(
                eTypeIsScalar)) // if this is not even a scalar...
        {
          if (options.m_synthetic_children_traversal ==
              GetValueOptions::SyntheticChildrenTraversal::
                  None) // ...only chance left is synthetic
          {
            *reason_to_stop = ScanEndReason::RangeOperatorInvalid;
            *final_result = EndResultType::Invalid;
            return ValueObjectSP();
          }
        } else if (!options.m_allow_bitfields_syntax) // if this is a scalar,
                                                      // check that we can
                                                      // expand bitfields
        {
          *reason_to_stop = ScanEndReason::RangeOperatorNotAllowed;
          *final_result = EndResultType::Invalid;
          return ValueObjectSP();
        }
      }
      if (temp_expression[1] ==
          ']') // if this is an unbounded range it only works for arrays
      {
        if (!root_compiler_type_info.Test(eTypeIsArray)) {
          *reason_to_stop = ScanEndReason::EmptyRangeNotAllowed;
          *final_result = EndResultType::Invalid;
          return nullptr;
        } else // even if something follows, we cannot expand unbounded ranges,
               // just let the caller do it
        {
          *reason_to_stop = ScanEndReason::ArrayRangeOperatorMet;
          *final_result = EndResultType::UnboundedRange;
          return root;
        }
      }

      size_t close_bracket_position = temp_expression.find(']', 1);
      if (close_bracket_position ==
          llvm::StringRef::npos) // if there is no ], this is a syntax error
      {
        *reason_to_stop = ScanEndReason::UnexpectedSymbol;
        *final_result = EndResultType::Invalid;
        return nullptr;
      }

      llvm::StringRef bracket_expr =
          temp_expression.slice(1, close_bracket_position);

      // If this was an empty expression it would have been caught by the if
      // above.
      assert(!bracket_expr.empty());

      if (!bracket_expr.contains('-')) {
        // if no separator, this is of the form [N].  Note that this cannot be
        // an unbounded range of the form [], because that case was handled
        // above with an unconditional return.
        unsigned long index = 0;
        if (bracket_expr.getAsInteger(0, index)) {
          *reason_to_stop = ScanEndReason::UnexpectedSymbol;
          *final_result = EndResultType::Invalid;
          return nullptr;
        }

        // from here on we do have a valid index
        if (root_compiler_type_info.Test(eTypeIsArray)) {
          ValueObjectSP child_valobj_sp = root->GetChildAtIndex(index, true);
          if (!child_valobj_sp)
            child_valobj_sp = root->GetSyntheticArrayMember(index, true);
          if (!child_valobj_sp)
            if (root->HasSyntheticValue() &&
                root->GetSyntheticValue()->GetNumChildren() > index)
              child_valobj_sp =
                  root->GetSyntheticValue()->GetChildAtIndex(index, true);
          if (child_valobj_sp) {
            root = child_valobj_sp;
            remainder =
                temp_expression.substr(close_bracket_position + 1); // skip ]
            *final_result = EndResultType::Plain;
            continue;
          } else {
            *reason_to_stop = ScanEndReason::NoSuchChild;
            *final_result = EndResultType::Invalid;
            return nullptr;
          }
        } else if (root_compiler_type_info.Test(eTypeIsPointer)) {
          if (*what_next == Aftermath::Dereference && // if this is a
                                                      // ptr-to-scalar, I
                                                      // am accessing it
                                                      // by index and I
                                                      // would have
                                                      // deref'ed anyway,
                                                      // then do it now
                                                      // and use this as
                                                      // a bitfield
              pointee_compiler_type_info.Test(eTypeIsScalar)) {
            Status error;
            root = root->Dereference(error);
            if (error.Fail() || !root) {
              *reason_to_stop = ScanEndReason::DereferencingFailed;
              *final_result = EndResultType::Invalid;
              return nullptr;
            } else {
              *what_next = Aftermath::Nothing;
              continue;
            }
          } else {
            if (root->GetCompilerType().GetMinimumLanguage() ==
                    eLanguageTypeObjC &&
                pointee_compiler_type_info.AllClear(eTypeIsPointer) &&
                root->HasSyntheticValue() &&
                (options.m_synthetic_children_traversal ==
                     GetValueOptions::SyntheticChildrenTraversal::ToSynthetic ||
                 options.m_synthetic_children_traversal ==
                     GetValueOptions::SyntheticChildrenTraversal::Both)) {
              root = root->GetSyntheticValue()->GetChildAtIndex(index, true);
            } else
              root = root->GetSyntheticArrayMember(index, true);
            if (!root) {
              *reason_to_stop = ScanEndReason::NoSuchChild;
              *final_result = EndResultType::Invalid;
              return nullptr;
            } else {
              remainder =
                  temp_expression.substr(close_bracket_position + 1); // skip ]
              *final_result = EndResultType::Plain;
              continue;
            }
          }
        } else if (root_compiler_type_info.Test(eTypeIsScalar)) {
          root = root->GetSyntheticBitFieldChild(index, index, true);
          if (!root) {
            *reason_to_stop = ScanEndReason::NoSuchChild;
            *final_result = EndResultType::Invalid;
            return nullptr;
          } else // we do not know how to expand members of bitfields, so we
                 // just return and let the caller do any further processing
          {
            *reason_to_stop = ScanEndReason::BitfieldRangeOperatorMet;
            *final_result = EndResultType::Bitfield;
            return root;
          }
        } else if (root_compiler_type_info.Test(eTypeIsVector)) {
          root = root->GetChildAtIndex(index, true);
          if (!root) {
            *reason_to_stop = ScanEndReason::NoSuchChild;
            *final_result = EndResultType::Invalid;
            return ValueObjectSP();
          } else {
            remainder =
                temp_expression.substr(close_bracket_position + 1); // skip ]
            *final_result = EndResultType::Plain;
            continue;
          }
        } else if (options.m_synthetic_children_traversal ==
                       GetValueOptions::SyntheticChildrenTraversal::
                           ToSynthetic ||
                   options.m_synthetic_children_traversal ==
                       GetValueOptions::SyntheticChildrenTraversal::Both) {
          if (root->HasSyntheticValue())
            root = root->GetSyntheticValue();
          else if (!root->IsSynthetic()) {
            *reason_to_stop = ScanEndReason::SyntheticValueMissing;
            *final_result = EndResultType::Invalid;
            return nullptr;
          }
          // if we are here, then root itself is a synthetic VO.. should be
          // good to go

          if (!root) {
            *reason_to_stop = ScanEndReason::SyntheticValueMissing;
            *final_result = EndResultType::Invalid;
            return nullptr;
          }
          root = root->GetChildAtIndex(index, true);
          if (!root) {
            *reason_to_stop = ScanEndReason::NoSuchChild;
            *final_result = EndResultType::Invalid;
            return nullptr;
          } else {
            remainder =
                temp_expression.substr(close_bracket_position + 1); // skip ]
            *final_result = EndResultType::Plain;
            continue;
          }
        } else {
          *reason_to_stop = ScanEndReason::NoSuchChild;
          *final_result = EndResultType::Invalid;
          return nullptr;
        }
      } else {
        // we have a low and a high index
        llvm::StringRef sleft, sright;
        unsigned long low_index, high_index;
        std::tie(sleft, sright) = bracket_expr.split('-');
        if (sleft.getAsInteger(0, low_index) ||
            sright.getAsInteger(0, high_index)) {
          *reason_to_stop = ScanEndReason::UnexpectedSymbol;
          *final_result = EndResultType::Invalid;
          return nullptr;
        }

        if (low_index > high_index) // swap indices if required
          std::swap(low_index, high_index);

        if (root_compiler_type_info.Test(
                eTypeIsScalar)) // expansion only works for scalars
        {
          root = root->GetSyntheticBitFieldChild(low_index, high_index, true);
          if (!root) {
            *reason_to_stop = ScanEndReason::NoSuchChild;
            *final_result = EndResultType::Invalid;
            return nullptr;
          } else {
            *reason_to_stop = ScanEndReason::BitfieldRangeOperatorMet;
            *final_result = EndResultType::Bitfield;
            return root;
          }
        } else if (root_compiler_type_info.Test(
                       eTypeIsPointer) && // if this is a ptr-to-scalar, I am
                                          // accessing it by index and I would
                                          // have deref'ed anyway, then do it
                                          // now and use this as a bitfield
                   *what_next == Aftermath::Dereference &&
                   pointee_compiler_type_info.Test(eTypeIsScalar)) {
          Status error;
          root = root->Dereference(error);
          if (error.Fail() || !root) {
            *reason_to_stop = ScanEndReason::DereferencingFailed;
            *final_result = EndResultType::Invalid;
            return nullptr;
          } else {
            *what_next = ExpressionPath::Aftermath::Nothing;
            continue;
          }
        } else {
          *reason_to_stop = ScanEndReason::ArrayRangeOperatorMet;
          *final_result = EndResultType::BoundedRange;
          return root;
        }
      }
      break;
    }
    default: // some non-separator is in the way
    {
      *reason_to_stop = ScanEndReason::UnexpectedSymbol;
      *final_result = EndResultType::Invalid;
      return nullptr;
    }
    }
  }
}

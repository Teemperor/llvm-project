//===-- EnumFlags.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ENUMFLAGS_H
#define LLDB_UTILITY_ENUMFLAGS_H

#include "llvm/ADT/ArrayRef.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lldb_private {

/// \class EnumFlags EnumFlags.h "lldb/Utility/EnumFlags.h"
/// Typed wrapper for enum-based bit flags.
///
/// EnumFlags provides functions for reading and writing specific bits in the
/// underlying flags storage. If possible, all functions are typed with the
/// actual enum that is used to describe the meaning of the different bits.
///
/// This class supports both scoped and unscoped enums.
template <typename T> class EnumFlags {
  /// The integer type that can store all the bits that can be described by
  /// the enumerators of this specialization.
  typedef std::underlying_type_t<T> ValueType;
  /// Utility function for converting the enum type to the used ValueType.
  static ValueType ToValueType(T e) { return static_cast<ValueType>(e); }

  /// Bitwise ORs all passed enumerators into a integer compatible with the
  /// internal integer storage for the flags.
  static ValueType MergeEnumArgs(llvm::ArrayRef<T> flags) {
    ValueType Merged = 0;
    for (T e : flags)
      Merged |= ToValueType(e);
    return Merged;
  }

public:
  EnumFlags() = default;
  explicit EnumFlags(llvm::ArrayRef<T> flags) { Set(flags); }

  /// Clears all flags.
  void Clear() { m_flags = 0; }

  /// Clears the bits for the given enumerator.
  void Clear(T e) { m_flags &= ~ToValueType(e); }

  /// Clears all bits described by any of the given enumerators.
  void Clear(llvm::ArrayRef<T> flags) { m_flags &= ~MergeEnumArgs(flags); }

  /// Sets the bits for the given enumerator.
  void Set(T e) { m_flags |= ToValueType(e); }

  /// Sets all bits described by any of the given enumerators.
  void Set(llvm::ArrayRef<T> flags) { m_flags |= MergeEnumArgs(flags); }

  /// Tests if all bits described by the given enumerator are set.
  /// \return True iff the respective bits are set.
  bool Test(T e) const { return (m_flags & ToValueType(e)) != 0; }

  /// Tests if all bits described by the given enumerator are not set.
  /// \return True iff the respective bits are not set.
  bool IsClear(T e) const { return !Test(e); }

  /// Returns true iff every bit that belongs to one of the passed enumerators
  /// is set.
  bool AllSet(llvm::ArrayRef<T> flags) const {
    ValueType mask = MergeEnumArgs(flags);
    return (m_flags & mask) == mask;
  }

  /// Returns true iff at least one bit that belongs to one of the passed
  /// enumerators is set.
  bool AnySet(llvm::ArrayRef<T> flags) const {
    return m_flags & MergeEnumArgs(flags);
  }

  /// Returns true iff every bit that belongs to one of the passed
  /// enumerators is set.
  bool AllClear(llvm::ArrayRef<T> flags) const {
    return (m_flags & MergeEnumArgs(flags)) == 0;
  }

  /// Returns true iff at least one bit that belongs to one of the passed
  /// enumerators is not set.
  bool AnyClear(llvm::ArrayRef<T> flags) const {
    ValueType mask = MergeEnumArgs(flags);
    return (m_flags & mask) != mask;
  }

  /// Returns the internal integer storage that is used to store the flag bits.
  /// All bits of the set enumerators are just bitwise or'd into this storage
  /// integer.
  /// \note This function is not using the enum type and can't do any type
  /// checking. It only exists to implement legacy interfaces from the SB API
  /// and should not be used anywhere else.
  ValueType GetRawEncoding() const { return m_flags; }

  /// Sets the internal integer storage that is used to store the flag bits
  /// to the given value. The passed value should be bitwise or'd enumerators
  /// of the respective enum of this EnumFlags specialization.
  /// \note This function is not using the enum type and can't do any type
  /// checking. It only exists to implement legacy interfaces from the SB API
  /// and should not be used anywhere else.
  void SetFromRawEncoding(ValueType t) { m_flags = t; }

private:
  /// The integer storage in which the actual
  ValueType m_flags = 0;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_ENUMFLAGS_H

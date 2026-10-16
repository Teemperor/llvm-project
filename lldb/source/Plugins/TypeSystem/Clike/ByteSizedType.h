//===-- ByteSizedType.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BYTESIZEDTYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BYTESIZEDTYPE_H

#include <cstdint>
#include <optional>
#include <utility>

namespace lldb_private {
namespace clike {

/// Mixin that adds a byte size to a type.
///
/// This is only used for types that do not derive their size from another type.
template <typename Base> class ByteSizedType : public Base {
public:
  /// \p byte_size is std::nullopt if unknown. Any remaining arguments are
  /// forwarded to \p Base.
  template <typename... Args>
  explicit ByteSizedType(std::optional<uint64_t> byte_size, Args &&...args)
      : Base(std::forward<Args>(args)...),
        m_byte_size(byte_size.value_or(kNoByteSize)) {}

  std::optional<uint64_t> GetByteSize() const override {
    return m_byte_size == kNoByteSize ? std::nullopt
                                      : std::optional<uint64_t>(m_byte_size);
  }

protected:
  /// Used by records, whose size might only be known after completion.
  void SetByteSize(std::optional<uint64_t> byte_size) {
    m_byte_size = byte_size.value_or(kNoByteSize);
  }

private:
  // Sentinel for an unknown size.
  static constexpr uint64_t kNoByteSize = UINT64_MAX;
  uint64_t m_byte_size = kNoByteSize;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BYTESIZEDTYPE_H

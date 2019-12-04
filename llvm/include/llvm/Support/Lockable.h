#pragma once

#include "RWMutex.h"

namespace llvm {

// Forward declarations used for Lockable's friend declarations.
template<typename T>
class UniqueAccess;
template<typename T>
class SharedAccess;

template<typename DataType>
class Lockable {
  DataType GuardedData;
  mutable llvm::sys::RWMutex Mutex;

  friend class UniqueAccess<DataType>;
  friend class SharedAccess<DataType>;
public:
  explicit Lockable(DataType Data = DataType()) : GuardedData(Data) {}
  Lockable(const Lockable<DataType> &Other) {
    SharedAccess<DataType> Access(Other);
    GuardedData = *Access;
  }

  Lockable(Lockable<DataType> &&Other) {
    UniqueAccess<DataType> Access(Other);
    GuardedData = std::move(*Access);
  }

  Lockable<DataType> &operator=(const Lockable<DataType> &Other) {
    if (&Other == this)
      return *this;

    UniqueAccess<DataType> LockSelf(*this);
    UniqueAccess<DataType> Access(Other);
    GuardedData = *Access;
    return *this;
  }
};

template<typename DataType>
class UniqueAccess {
  static_assert(!std::is_const<DataType>::value,
                "Use SharedAccess<T> for const data types");
  DataType& Data;

  llvm::sys::ScopedWriter Guard;
public:
  explicit UniqueAccess(Lockable<DataType>& L) : Data(L.GuardedData),
    Guard(L.Mutex) {}
  explicit UniqueAccess(const Lockable<DataType>& L) = delete;

  DataType& operator *() {
    return Data;
  }
  DataType* operator->() {
    return &Data;
  }
};

template<typename DataType>
class SharedAccess {
  const DataType &Data;

  llvm::sys::ScopedReader Guard;
public:
  SharedAccess(const Lockable<DataType>& L)
    : Data(L.GuardedData), Guard(L.Mutex) {
  }

  const DataType& operator *() {
    return Data;
  }
  const DataType* operator->() {
    return &Data;
  }
};

}

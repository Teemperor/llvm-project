/// A metadata record whose kind is MetadataKind::Opaque. The Swift runtime's
/// metadata reader represents metadata it cannot interpret as an opaque type,
/// which demangles into an OpaqueType node without any children. The test
/// points the metadata pointer of a generic parameter at this record, which is
/// what happens in the wild when that pointer is read before the prologue of a
/// generic function has initialized it.
var fakeOpaqueMetadata: UInt = 0x300

struct Box<T> {
  let value: T

  func get() -> T {
    return value // break here
  }
}

func test() -> Int {
  let box = Box(value: 42)
  return box.get()
}

print(test())

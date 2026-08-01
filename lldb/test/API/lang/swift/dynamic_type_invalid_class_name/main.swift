class C {}

/// Fabricates something that looks enough like an Objective-C class object for
/// the Swift runtime's metadata reader to read a class name out of it, and
/// returns a pointer to an "instance" of that class.
///
/// The layout used here is the one the metadata reader expects: objc_class is
/// { isa, superclass, cache[2], data }, and the class_ro_t that `data` points
/// to has its 32-bit flags at offset 0 and the class name at offset 0x18.
func makeFakeInstance(name: [UInt8]) -> UnsafeMutablePointer<UInt> {
  let nameBuffer = UnsafeMutablePointer<UInt8>.allocate(capacity: name.count)
  nameBuffer.initialize(from: name, count: name.count)

  let ro = UnsafeMutablePointer<UInt>.allocate(capacity: 8)
  ro.initialize(repeating: 0, count: 8)
  ro[3] = UInt(bitPattern: nameBuffer)

  let cls = UnsafeMutablePointer<UInt>.allocate(capacity: 32)
  cls.initialize(repeating: 0, count: 32)
  // Any value larger than the last enumerated metadata kind makes the metadata
  // reader treat this as an Objective-C class instead of Swift metadata.
  cls[0] = 0x1000
  cls[4] = UInt(bitPattern: ro)

  let instance = UnsafeMutablePointer<UInt>.allocate(capacity: 8)
  instance.initialize(repeating: 0, count: 8)
  instance[0] = UInt(bitPattern: cls)
  return instance
}

// A class name that isn't valid UTF-8.
let invalidUTF8 = makeFakeInstance(name: [0xe0, 0xfe, 0x58, 0xfc, 0x01, 0x00])
// A class without a name.
let unnamed = makeFakeInstance(name: [0x00])

// unowned(unsafe) keeps ARC from touching these non-objects.
unowned(unsafe) let invalid_utf8_name = unsafeBitCast(invalidUTF8, to: C.self)
unowned(unsafe) let empty_name = unsafeBitCast(unnamed, to: C.self)

print("break here")
_ = invalid_utf8_name
_ = empty_name

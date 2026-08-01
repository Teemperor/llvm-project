/// A fabricated metadata record whose kind is MetadataKind::Opaque. The Swift
/// runtime's metadata reader represents metadata that it cannot interpret as an
/// opaque type, which cannot be remangled. The test points the metadata pointer
/// of an existential container at this record, which is what happens in the wild
/// when the container is read before it has been initialized.
///
/// Projecting an existential container also reads the value witness table that
/// precedes the metadata record, so the test copies a real one in front of the
/// fabricated kind.
struct FakeMetadata {
  var valueWitnessTable: UInt = 0
  var kind: UInt = 0
}

var fakeMetadata = FakeMetadata()

func use(_ value: Any) {
  print(value) // break here
}

use(42)

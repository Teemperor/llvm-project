protocol Container {
  associatedtype Element
}

struct ConcreteContainer<E>: Container {
  typealias Element = E
}

func makeCollection<C: Container>(_ c: C, _ e: C.Element)
    -> some Collection<C.Element> {
  return [e]
}

// The debug info type of this global is
// Array<ConcreteContainer<Double>.Element>. The Swift compiler doesn't describe
// the nested type alias in the debug info, so the element type has to be
// recovered from the runtime metadata of the array's storage.
let doubles = makeCollection(ConcreteContainer<Double>(), 42.5)

func stop() {
  print(doubles) // break here
}

stop()

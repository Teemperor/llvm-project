public struct A {
  var i = 23
}
public struct B {
  var d = 2.71
}

public protocol Defaulted {
  init()
}
extension Int: Defaulted {}
extension Double: Defaulted {}

// The pack expansion in the return type is not a value pack argument.
func returnsPack<each T>(_ n: Int, _ t: repeat each T) -> (repeat each T) {
  print("break here") // returnsPack
  return (repeat each t)
}

// The pack expansion nested inside the tuple parameter is not a value pack
// argument.
func tupleParam<each T>(_ n: Int, _ x: (repeat each T)) {
  print("break here") // tupleParam
}

// The pack expansion nested inside the function parameter is not a value pack
// argument.
func functionParam<each T>(_ n: Int, _ x: (repeat each T) -> Void) {
  print("break here") // functionParam
}

// This pack only shows up in the return type.
func onlyInReturn<each T: Defaulted>(_ n: Int) -> (repeat each T) {
  print("break here") // onlyInReturn
  return (repeat (each T)())
}

public struct Wrapper<each T> {
  var values: (repeat each T)
}

// The type of the local variable `w` mentions a pack, but `w` is an ordinary
// single value and not a value pack.
func mentionsPack<each T>(_ n: Int, _ t: repeat each T) -> (repeat each T) {
  let w = Wrapper(values: (repeat each t))
  print("break here") // mentionsPack
  _ = w
  return (repeat each t)
}

_ = returnsPack(1, A(), B())
tupleParam(2, (A(), B()))
functionParam(3) { (_: A, _: B) in }
let _: (Int, Double) = onlyInReturn(4)
_ = mentionsPack(5, A(), B())

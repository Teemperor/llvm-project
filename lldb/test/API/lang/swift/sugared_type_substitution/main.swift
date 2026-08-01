// The debug info type of `kp` is
// WritableKeyPath<f() -> [Double].(S), [[Double]]>, where the [[Double]] is
// spelled with the debugger-only "sugared array" mangling, while the [Double]
// in the enclosing function's signature is spelled out as Array<Double>.
// Desugaring the former has to produce a demangle tree that is structurally
// identical to the latter, otherwise the remangler cannot substitute the
// repeated Array<Double> and produces a non-canonical mangled name.
func f() -> [Double] {
  struct S {
    var values: [[Double]] = [[1.5, 2.5]]
  }
  let kp = \S.values
  let s = S()
  print(s[keyPath: kp]) // break here
  return [1.5]
}

_ = f()

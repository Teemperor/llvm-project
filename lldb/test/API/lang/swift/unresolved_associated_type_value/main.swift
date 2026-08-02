protocol Printable {
  func describe() -> String
}

protocol Container {
  associatedtype Element
  var items: [Element] { get }
}

extension Container where Element: Printable {
  func printAll() -> String {
    return items.map { $0.describe() }.joined(separator: ", ") // break here
  }
}

class Animal: Printable {
  var name: String
  init(name: String) { self.name = name }
  func describe() -> String { return "Animal: \(name)" }
}

struct Stack: Container {
  var items: [Animal] = []
}

let stack = Stack(items: [Animal(name: "Rex")])
print(stack.printAll())

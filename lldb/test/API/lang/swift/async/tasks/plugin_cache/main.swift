func work(_ id: Int, _ count: Int) async -> Int {
  var total = 0
  for i in 0..<count {
    total += i * id // break in task
    await Task.yield()
  }
  return total
}

@main struct Main {
  static func main() async {
    async let first = work(1, 30)
    async let second = work(2, 30)
    let total = await first + (await second)
    print(total) // break at end
  }
}

// Main links the hub dylib and all five leaf dylibs. It forward
// declares 'AllPayloads' itself and only gets a pointer to it from
// MakeAllPayloads(), so it never sees any of the five conflicting
// 'struct Payload' definitions -- those only get pulled into LLDB's
// shared scratch AST context when the test expression below runs.
struct AllPayloads;

extern "C" AllPayloads *MakeAllPayloads(void);

void hub_entry() {
  AllPayloads *hub = MakeAllPayloads();
  (void)hub;
}

int main() {
  hub_entry();
  return 0;
}

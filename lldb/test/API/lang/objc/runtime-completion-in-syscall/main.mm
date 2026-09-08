#import "HiddenClass.h"

#include <chrono>
#include <thread>

volatile int release_flag = 0;
// A global (not a frame-local), so it can be found regardless of which
// thread/frame the interrupt below happens to land the debugger's selection
// on -- likely somewhere inside the sleep syscall, not `main`.
HiddenClass *object;

int main(int argc, const char *argv[]) {
  object = [[HiddenClass alloc] init];

  // Block in a syscall, exactly like commands/expression/expr-in-syscall's
  // TestExpressionInSyscall: this leaves the ObjC runtime's
  // isa-to-descriptor map (see AppleObjCRuntimeV2::UpdateISAToDescriptorMap)
  // cold for the stop the test interrupts into below, so the first "frame
  // variable" touching `object`'s runtime-only ivars is what triggers the
  // map's (JIT-compiled) rebuild -- not some earlier breakpoint stop.
  while (!release_flag) // Wait for debugger to attach // breakpoint1
    std::this_thread::sleep_for(std::chrono::seconds(3));

  return object == nil;
}

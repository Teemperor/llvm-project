#import "HiddenClass.h"
#import <stdint.h>

// Compiled with -g0 (see the Makefile): the ivars below exist only in the
// Objective-C runtime metadata, not in any DWARF this test's debug info can
// see, so completing this class's children forces
// TypeSystemClike::GetRuntimeCompletedObjCType to build them from the live
// runtime instead.
@interface HiddenClass () {
@public
  uintptr_t foo;
  uintptr_t bar;
}
@end

@implementation HiddenClass

- (id)init {
  if (self = [super init]) {
    foo = 2;
    bar = 3;
  }
  return self;
}

@end

// RUN: %clang_cc1 %s -fsyntax-only -fobjc-exceptions -verify -Wreturn-type -Wmissing-noreturn -Wno-unreachable-code -Wno-covered-switch-default

@interface AnObj
- (void) Meth;
@property int prop;
@end

@class NSException;

int tryNested(AnObj *o) {
  @try {
    @try {
      [o Meth];
    } @catch (NSException *e) {
    } @finally {
      return 0;
    }
  } @catch (...) {
  } @finally {
  }
}

#ifdef ALL

int tryEmpty() {
  @try {
  } @catch (NSException *e) {
  }
} // expected-warning {{non-void function does not return a value}}

int tryEmptyCatchAll() {
  @try {
  } @catch (...) {
  }
} // expected-warning {{non-void function does not return a value}}

int tryEmptyCatchAllFinallyEmpty() {
  @try {
  } @catch (...) {
  } @finally {
  }
} // expected-warning {{non-void function does not return a value}}

int tryReturnOnlyInTry() {
  @try {
    return 0;
  } @catch (...) {
  }
}

int tryCallInTry(AnObj *o) {
  @try {
    [o Meth];
  } @catch (...) {
  }
} // expected-warning {{non-void function does not return a value}}

int tryCallInTryReturnInCatchAll(AnObj *o) {
  @try {
    [o Meth];
    return 0;
  } @catch (...) {
  }
} // expected-warning {{non-void function does not return a value}}

int tryProp(AnObj *o) {
  @try {
    // This might throw an exception depending how the property getter is
    // is implemented.
    return o.prop;
  } @catch (...) {
  }
} // expected-warning {{non-void function does not return a value}}

int tryReturnInTryAndCatchAll(AnObj *o) {
  @try {
    [o Meth];
    return 0;
  } @catch (...) {
    return 0;
  }
}

int tryReturnInTryAndIdCatchAll(AnObj *o) {
  @try {
    [o Meth];
    return 0;
  } @catch (id) {
    // @catch (id) is equal to @catch (...).
    return 0;
  }
}

int tryReturnInTryAndSpecificException(AnObj *o) {
  @try {
    [o Meth];
    return 0;
  } @catch (NSException *e) {
    return 0;
  }
} // expected-warning {{non-void function does not return a value}}

int tryReturnAfterCatchAll() {
  @try {
  } @catch (...) {
  }
  return 0;
}

int tryEmptyFinallyWithReturnSpecificCatch(AnObj *o) {
  @try {
    [o Meth];
  } @catch (NSException *e) {
  } @finally {
    return 0;
  }
}

int tryEmptyFinallyWithReturn(AnObj *o) {
  @try {
  } @catch (...) {
  } @finally {
    return 0;
  }
}

int tryFinallyWithReturn(AnObj *o) {
  @try {
    // Could raise an exception but the @finally ensures we always return.
    [o Meth];
  } @catch (...) {
  } @finally {
    return 0;
  }
}

int tryFinallyWithPotentialException(AnObj *o) {
  @try {
  } @catch (...) {
  } @finally {
    // Could raise an exception but we pretend it doesn't as there is no
    // active @try scope.
    [o Meth];
    return 0;
  }
}

#endif

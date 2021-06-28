// RUN: %clang_cc1 %s -fsyntax-only -fobjc-exceptions -verify -Wreturn-type -Wmissing-noreturn -Wno-unreachable-code -Wno-covered-switch-default

@interface AnObj
- (void) Meth;
@property int prop;
@end

int bla(AnObj *o) {
  [o Meth];
  return 0;
}

int tryEmpty() {
   @try {
   } @catch (...) {
   }
} // expected-warning {{non-void function does not return a value}}

int tryReturnInTry() {
   @try {
     return 0;
   } @catch (...) {
   }
}

int tryCallInTry(AnObj *o) {
   @try {
     [o Meth];
     return 0;
   } @catch (...) {
   }
} // expected-warning {{non-void function does not return a value}}

int tryPropInTry(AnObj *o) {
   @try {
     return o.prop;
   } @catch (...) {
   }
} // expected-warning {{non-void function does not return a value}}

int tryReturnInTryAndCatchAll() {
   @try {
     return 0;
   } @catch (...) {
     return 0;
   }
}

int tryReturnAfterCatchAll() {
   @try {
   } @catch (...) {
   }
   return 0;
}

int tryEmptyFinallyEmpty() {
   @try {
   } @catch (...) {
   } @finally {
   }
} // expected-warning {{non-void function does not return a value}}

int tryEmptyFinallyWithReturn(AnObj *o) {
   @try {
   } @catch (...) {
   } @finally {
     return 0;
   }
}

int tryFinallyWithReturn(AnObj *o) {
   @try {
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
     [o Meth];
     return 0;
   }
}

// Verify that the desired DIExpression are generated for noescape blocks.

// RUN: %clang_cc1 -fblocks -debug-info-kind=limited -emit-llvm -o - %s | FileCheck %s

typedef void (^BlockTy)();
void noEscapeFunc(__attribute__((noescape)) BlockTy);

void test() {
// CHECK-LABEL: void @test
// CHECK: call void @llvm.dbg.declare({{.*}}!DIExpression())
// Prevent accidential passes due to DIExpressions() in the block init code.
// CHECK-LABEL: %block.isa =
  __block int i;
  noEscapeFunc(^{ (void)i; });
}

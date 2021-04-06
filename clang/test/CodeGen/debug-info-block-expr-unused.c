// Verify that the desired DIExpression are generated for __block vars not used
// in any block.

// RUN: %clang_cc1 -fblocks -debug-info-kind=limited -emit-llvm -o - %s | FileCheck %s

typedef void (^BlockTy)();
void noEscapeFunc(__attribute__((noescape)) BlockTy);

int test() {
// CHECK-LABEL: i32 @test
// CHECK: call void @llvm.dbg.declare({{.*}}!DIExpression())
  __block int i;
// Use i (not inside a block).
  ++i;
// Prevent accidential passes due to DIExpressions() in the block init code.
// CHECK-LABEL: ret i32 1234
  return 1234;
}

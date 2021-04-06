// Verify that the desired DIExpression are generated for escaping (i.e, not
// 'noescape') blocks.

// RUN: %clang_cc1 -fblocks -debug-info-kind=limited -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -DDEAD_CODE -fblocks -debug-info-kind=limited -emit-llvm -o - %s | FileCheck %s

typedef void (^BlockTy)();
void escapeFunc(BlockTy);

void test() {
// CHECK-LABEL: void @test
// CHECK: call void @llvm.dbg.declare({{.*}}!DIExpression(DW_OP_plus_uconst, {{[0-9]+}}, DW_OP_deref, DW_OP_plus_uconst, {{[0-9]+}}){{.*}})
// Prevent accidential passes due to DIExpressions() in the block init code.
// CHECK-LABEL: %byref.isa =
  __block int i;
// Blocks in dead code branches still capture __block variables.
#ifdef DEAD_CODE
  if (0)
#endif
  escapeFunc(^{ (void)i; });
}

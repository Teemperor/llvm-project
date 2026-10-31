; RUN: llc -O0 -mtriple=x86_64-unknown-linux-gnu %s -o %t -filetype=obj
; RUN: llvm-dwarfdump -debug-info %t | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -O0 -mtriple=x86_64-unknown-linux-gnu -debug-fwd-decl-locations %s -o %t -filetype=obj
; RUN: llvm-dwarfdump -debug-info %t | FileCheck %s

; Forward declarations only get DW_AT_decl_file/DW_AT_decl_line with
; -debug-fwd-decl-locations and if they have a line number.

; CHECK:      DW_TAG_structure_type
; CHECK-NEXT:   DW_AT_name ("WithLoc")
; CHECK-NEXT:   DW_AT_declaration (true)
; CHECK-NEXT:   DW_AT_decl_file ("/tmp/foo.cpp")
; CHECK-NEXT:   DW_AT_decl_line (3)
; CHECK:      DW_TAG_structure_type
; CHECK-NEXT:   DW_AT_name ("NoLoc")
; CHECK-NEXT:   DW_AT_declaration (true)
; CHECK-EMPTY:

; DEFAULT:      DW_TAG_structure_type
; DEFAULT-NEXT:   DW_AT_name ("WithLoc")
; DEFAULT-NEXT:   DW_AT_declaration (true)
; DEFAULT-EMPTY:
; DEFAULT:      DW_TAG_structure_type
; DEFAULT-NEXT:   DW_AT_name ("NoLoc")
; DEFAULT-NEXT:   DW_AT_declaration (true)
; DEFAULT-EMPTY:

@a = global ptr null, align 8, !dbg !0
@b = global ptr null, align 8, !dbg !8

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!13}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "a", scope: !2, file: !3, line: 5, type: !6, isLocal: false, isDefinition: true)
!2 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !3, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, globals: !5)
!3 = !DIFile(filename: "foo.cpp", directory: "/tmp")
!5 = !{!0, !8}
!6 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !7, size: 64)
!7 = !DICompositeType(tag: DW_TAG_structure_type, name: "WithLoc", file: !3, line: 3, size: 32, flags: DIFlagFwdDecl, identifier: "_ZTS7WithLoc")
!8 = !DIGlobalVariableExpression(var: !9, expr: !DIExpression())
!9 = distinct !DIGlobalVariable(name: "b", scope: !2, file: !3, line: 6, type: !10, isLocal: false, isDefinition: true)
!10 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !11, size: 64)
!11 = !DICompositeType(tag: DW_TAG_structure_type, name: "NoLoc", file: !3, flags: DIFlagFwdDecl, identifier: "_ZTS5NoLoc")
!13 = !{i32 2, !"Debug Info Version", i32 3}

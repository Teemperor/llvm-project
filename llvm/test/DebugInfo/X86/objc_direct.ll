; RUN: llc < %s -filetype=obj -o %t
; RUN: llvm-dwarfdump -v %t | FileCheck %s

; Source code to regenerate:
; __attribute__((objc_root_class))
; @interface Root
; - (int)direct_method __attribute__((objc_direct));
; @end
;
; @implementation Root
; - (int)direct_method __attribute__((objc_direct)) {
;   return 42;
; }
; @end
;
; clang -O0 -g -gdwarf-5 direct.m -c

; CHECK: DW_AT_APPLE_objc_direct
; CHECK-SAME: DW_FORM_flag_present

; ModuleID = 'direct.bc'
source_filename = "direct.m"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.15.0"

%0 = type opaque
%struct._objc_cache = type opaque
%struct._class_t = type { %struct._class_t*, %struct._class_t*, %struct._objc_cache*, i8* (i8*, i8*)**, %struct._class_ro_t* }
%struct._class_ro_t = type { i32, i32, i32, i8*, i8*, %struct.__method_list_t*, %struct._objc_protocol_list*, %struct._ivar_list_t*, i8*, %struct._prop_list_t* }
%struct.__method_list_t = type { i32, i32, [0 x %struct._objc_method] }
%struct._objc_method = type { i8*, i8*, i8* }
%struct._objc_protocol_list = type { i64, [0 x %struct._protocol_t*] }
%struct._protocol_t = type { i8*, i8*, %struct._objc_protocol_list*, %struct.__method_list_t*, %struct.__method_list_t*, %struct.__method_list_t*, %struct.__method_list_t*, %struct._prop_list_t*, i32, i32, i8**, i8*, %struct._prop_list_t* }
%struct._ivar_list_t = type { i32, i32, [0 x %struct._ivar_t] }
%struct._ivar_t = type { i64*, i8*, i8*, i32, i32 }
%struct._prop_list_t = type { i32, i32, [0 x %struct._prop_t] }
%struct._prop_t = type { i8*, i8* }

@_objc_empty_cache = external global %struct._objc_cache
@"OBJC_CLASS_$_Root" = global %struct._class_t { %struct._class_t* @"OBJC_METACLASS_$_Root", %struct._class_t* null, %struct._objc_cache* @_objc_empty_cache, i8* (i8*, i8*)** null, %struct._class_ro_t* @"_OBJC_CLASS_RO_$_Root" }, section "__DATA, __objc_data", align 8
@"OBJC_METACLASS_$_Root" = global %struct._class_t { %struct._class_t* @"OBJC_METACLASS_$_Root", %struct._class_t* @"OBJC_CLASS_$_Root", %struct._objc_cache* @_objc_empty_cache, i8* (i8*, i8*)** null, %struct._class_ro_t* @"_OBJC_METACLASS_RO_$_Root" }, section "__DATA, __objc_data", align 8
@OBJC_CLASS_NAME_ = private unnamed_addr constant [5 x i8] c"Root\00", section "__TEXT,__objc_classname,cstring_literals", align 1
@"_OBJC_METACLASS_RO_$_Root" = internal global %struct._class_ro_t { i32 3, i32 40, i32 40, i8* null, i8* getelementptr inbounds ([5 x i8], [5 x i8]* @OBJC_CLASS_NAME_, i32 0, i32 0), %struct.__method_list_t* null, %struct._objc_protocol_list* null, %struct._ivar_list_t* null, i8* null, %struct._prop_list_t* null }, section "__DATA, __objc_const", align 8
@"_OBJC_CLASS_RO_$_Root" = internal global %struct._class_ro_t { i32 2, i32 0, i32 0, i8* null, i8* getelementptr inbounds ([5 x i8], [5 x i8]* @OBJC_CLASS_NAME_, i32 0, i32 0), %struct.__method_list_t* null, %struct._objc_protocol_list* null, %struct._ivar_list_t* null, i8* null, %struct._prop_list_t* null }, section "__DATA, __objc_const", align 8
@"OBJC_LABEL_CLASS_$" = internal global [1 x i8*] [i8* bitcast (%struct._class_t* @"OBJC_CLASS_$_Root" to i8*)], section "__DATA,__objc_classlist,regular,no_dead_strip", align 8
@llvm.compiler.used = appending global [2 x i8*] [i8* getelementptr inbounds ([5 x i8], [5 x i8]* @OBJC_CLASS_NAME_, i32 0, i32 0), i8* bitcast ([1 x i8*]* @"OBJC_LABEL_CLASS_$" to i8*)], section "llvm.metadata"
; Function Attrs: noinline optnone ssp uwtable
define hidden i32 @"\01-[Root direct_method]"(%0* %self, i8* %_cmd) #0 !dbg !24 {
entry:
  %retval = alloca i32, align 4
  %self.addr = alloca %0*, align 8
  %_cmd.addr = alloca i8*, align 8
  store %0* %self, %0** %self.addr, align 8
  call void @llvm.dbg.declare(metadata %0** %self.addr, metadata !25, metadata !DIExpression()), !dbg !27
  store i8* %_cmd, i8** %_cmd.addr, align 8
  call void @llvm.dbg.declare(metadata i8** %_cmd.addr, metadata !28, metadata !DIExpression()), !dbg !27
  %0 = load %0*, %0** %self.addr, align 8, !dbg !30
  %1 = icmp eq %0* %0, null, !dbg !30
  br i1 %1, label %objc_direct_method.self_is_nil, label %objc_direct_method.cont, !dbg !30, !prof !31

objc_direct_method.self_is_nil:                   ; preds = %entry
  %2 = bitcast i32* %retval to i8*, !dbg !30
  call void @llvm.memset.p0i8.i64(i8* align 4 %2, i8 0, i64 4, i1 false), !dbg !30
  br label %return, !dbg !30

objc_direct_method.cont:                          ; preds = %entry
  store i32 42, i32* %retval, align 4, !dbg !32
  br label %return, !dbg !32

return:                                           ; preds = %objc_direct_method.cont, %objc_direct_method.self_is_nil
  %3 = load i32, i32* %retval, align 4, !dbg !33
  ret i32 %3, !dbg !33
}
; Function Attrs: nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1
; Function Attrs: argmemonly nounwind willreturn
declare void @llvm.memset.p0i8.i64(i8* nocapture writeonly, i8, i64, i1 immarg) #2

attributes #0 = { noinline optnone ssp uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+cx8,+fxsr,+mmx,+sahf,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone speculatable willreturn }
attributes #2 = { argmemonly nounwind willreturn }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!14, !15, !16, !17, !18, !19, !20, !21, !22}
!llvm.ident = !{!23}

!0 = distinct !DICompileUnit(language: DW_LANG_ObjC, file: !1, producer: "clang version 10.0.0 (https://github.com/llvm/llvm-project d6b2f33e2b6338d24cf756ba220939aecc81210d)", isOptimized: false, runtimeVersion: 2, emissionKind: FullDebug, enums: !2, retainedTypes: !3, nameTableKind: None)
!1 = !DIFile(filename: "direct.m", directory: "/", checksumkind: CSK_MD5, checksum: "6b49fad130344b0011fc0eef65949390")
!2 = !{}
!3 = !{!4}
!4 = !DICompositeType(tag: DW_TAG_structure_type, name: "Root", scope: !1, file: !1, line: 2, flags: DIFlagObjcClassComplete, elements: !5, runtimeLang: DW_LANG_ObjC)
!5 = !{!6}
!6 = !DISubprogram(name: "-[Root direct_method]", scope: !4, file: !1, line: 7, type: !7, scopeLine: 7, flags: DIFlagPrototyped, spFlags: DISPFlagDirect, retainedNodes: !2)
!7 = !DISubroutineType(types: !8)
!8 = !{!9, !10, !11}
!9 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!10 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64, flags: DIFlagArtificial | DIFlagObjectPointer)
!11 = !DIDerivedType(tag: DW_TAG_typedef, name: "SEL", file: !1, baseType: !12, flags: DIFlagArtificial)
!12 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !13, size: 64)
!13 = !DICompositeType(tag: DW_TAG_structure_type, name: "objc_selector", file: !1, flags: DIFlagFwdDecl)
!14 = !{i32 1, !"Objective-C Version", i32 2}
!15 = !{i32 1, !"Objective-C Image Info Version", i32 0}
!16 = !{i32 1, !"Objective-C Image Info Section", !"__DATA,__objc_imageinfo,regular,no_dead_strip"}
!17 = !{i32 4, !"Objective-C Garbage Collection", i32 0}
!18 = !{i32 1, !"Objective-C Class Properties", i32 64}
!19 = !{i32 7, !"Dwarf Version", i32 5}
!20 = !{i32 2, !"Debug Info Version", i32 3}
!21 = !{i32 1, !"wchar_size", i32 4}
!22 = !{i32 7, !"PIC Level", i32 2}
!23 = !{!"clang version 10.0.0 (https://github.com/llvm/llvm-project d6b2f33e2b6338d24cf756ba220939aecc81210d)"}
!24 = distinct !DISubprogram(name: "-[Root direct_method]", scope: !1, file: !1, line: 7, type: !7, scopeLine: 7, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, declaration: !6, retainedNodes: !2)
!25 = !DILocalVariable(name: "self", arg: 1, scope: !24, type: !26, flags: DIFlagArtificial | DIFlagObjectPointer)
!26 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!27 = !DILocation(line: 0, scope: !24)
!28 = !DILocalVariable(name: "_cmd", arg: 2, scope: !24, type: !29, flags: DIFlagArtificial)
!29 = !DIDerivedType(tag: DW_TAG_typedef, name: "SEL", file: !1, baseType: !12)
!30 = !DILocation(line: 7, column: 1, scope: !24)
!31 = !{!"branch_weights", i32 1, i32 1048576}
!32 = !DILocation(line: 8, column: 3, scope: !24)
!33 = !DILocation(line: 9, column: 1, scope: !24)

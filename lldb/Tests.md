# TypeSystemClike test status

## Objective-C status (2026-07-16)

Environment: arm64 macOS, `Release/bin/lldb`, `symbols.enable-typesystem-clike=true`.

### Regression gate: `lb Release -f lang/c` (runs lang/c + lang/cpp)
Clean — 0 failures (2026-07-24 re-run). The former dynamic-value bucket is
gone: `TestDynamicValue.py` (all variants, incl. the .dSYM config) and
`TestDynamicValueSameBase.py` now pass under TypeSystemClike. The two
`symbols.enable-typesystem-clike` skips added for the dSYM virtual-base gap
(commit 419fb70845dd) were removed once `ClangASTGenerator::
ComputeVBaseOffsetOffset` (commit 0afbc1e7d9e8) recovered the vtable-relative
vbase offset that dsymutil strips — so there is nothing left to un-gate here.

```
  Unsupported      :    9 (0.40%)
  Passed           :  186 (8.29%)
  Expectedly Failed:    5 (0.22%)
  Failed           :    0
```

### lang/objc: `lb Release -f lang/objc`
```
  Passed           :   17 (0.76%)
  Expectedly Failed:    1 (0.04%)
  Unsupported      :    2 (0.09%)
  Unresolved       :   30 (1.34%)
  Failed           :   36 (1.61%)
```

PASS (17 files fully passing), incl. the three required tests:
TestObjCBaseClassSBType, TestObjCIvarOffsets, TestRealDefinition (all confirmed).
Others: TestCoalescedCFTypes, TestCppKeywordsAsObjCIdentifiers,
TestDynamicTypeChildren, TestFoundationDisassembly, TestSymbolTable,
TestObjCLanguageSpecificData, TestClangModuleImportLog, TestClangModulesAppUpdate,
TestClangModulesCache, TestClangModulesHashMismatch, TestClangModulesUpdate,
TestObjCDynamicValue, TestObjCStructDescription, TestSynthesizedPropertyAccessor.

FAIL (36): mostly ObjC expression evaluation not routed through the Cpp path —
"Expression evaluation in pure Objective-C not supported", "use of undeclared
identifier" for ObjC ivars/classes/locals (NSString, NSError, member, m_a,
elements, myId), "Size of result type '' couldn't be determined", plus a few
value/dynamic-type mismatches (id vs concrete class, formatter-dependent
Foundation string checks) and stepping differences.

UNRESOLVED (30) — lldb crashes, two signatures:

1. SIGSEGV (-11), ~22 tests — infinite recursion / stack overflow in the Cpp
   expression decl map. Cycle:
   `ClikeExpressionDeclMap::FindExternalVisibleDecls` ->
   `LookupSymbolFunction` -> `ClangASTGenerator::GenerateGenericFunction` ->
   `BuildFunction` -> `DeclContext::makeDeclVisibleInContext` ->
   `CppASTSourceProxy::FindExternalVisibleDeclsByName` -> (repeats).
   Bottom frame lexes via `CPlusPlusNameParser::ExtractTokens` / clang Lexer.
   Tests: TestObjCNewSyntaxLiteral, TestObjCStaticMethod, TestConflictingDefinition,
   TestObjCCheckers, TestObjCWarningsInExprParser, TestObjCIsTypeComplete,
   TestObjCNewSyntaxArray, TestObjCStaticMethodStripped, TestCharStarDynType,
   TestUnicodeString, TestOrderedSet, TestConstStrings, TestRuntimeIvars,
   TestObjCXX, TestObjCNewSyntaxDictionary, TestObjCFailingDescription,
   TestObjCClassMethod, TestObjCModules, TestIncompleteModules, TestObjCStructReturn,
   (TestObjCMethodReturningBOOL / TestObjCMethodsString / TestObjCMethods2 /
   TestObjCMethodsNSArray reach the assert below on some methods, this one on others).

2. SIGABRT (-6) — clang assertion
   `checkBitfieldClipping: (M.Offset >= Tail && "Bitfield access unit is not clipped"),
   CGRecordLayoutBuilder.cpp:960` when clang lays out an ObjC record with ivars.
   FIXED: ClangASTGenerator now generates a clang ObjCInterfaceDecl (with
   ObjCIvarDecls + superclass) for clike_typesystem::ObjCInterfaceType instead of a
   CXXRecordDecl, and a pointer to it becomes an ObjCObjectPointerType. The
   formerly-crashing tests (TestHiddenIvars, TestIvarInFrameworkBase,
   TestBitfieldIvars, TestObjCIvarsInBlocks, TestObjCIvarStripped,
   TestObjCMethodsString) no longer abort; they now fail cleanly on the separate
   ObjC-expression gap in bucket #1 above ("undeclared identifier" for an
   unqualified ivar; runtime-only "hidden" ivars not modeled). `self->ivar`
   member access through the ObjC pointer now works.
   A few -6 aborts are instead "Program aborted due to an unhandled Error:
   Could not install utility function / current process state is unsuitable for
   expression parsing" (TestObjCMethods2, TestObjCMethodsNSArray, TestPtrRefsObjC,
   TestObjCMethodReturningBOOL).

### lang/objcxx: `lb Release -f lang/objcxx`
```
  Passed  :    3 (0.13%)
  Failed  :    5 (0.22%)
```
FAIL (5): TestObjCConflictingNamesForClassUpdateExpr, TestObjCXXBridgedPO,
TestObjCBuiltinTypes, TestObjCFromCppFramesWithoutDebugInfo,
TestIvarVector (expr "elements" -> use of undeclared identifier).
No crashes/unresolved in objcxx.

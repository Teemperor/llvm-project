// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -gomitted-type-locations %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=DEFAULT

// Forward declarations of types whose definition was omitted keep pointing to
// the definition. Forward declarations of types without a definition don't
// have a source location with -gomitted-type-locations.

struct Incomplete;
enum class IncompleteEnum : int;

struct Dynamic {
  virtual ~Dynamic();
  int x;
};

Dynamic *d;
Incomplete *i;
IncompleteEnum *e;

// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Dynamic", file: ![[FILE:[0-9]+]], line: 11, size: 128, flags: DIFlagFwdDecl
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Incomplete", file: ![[FILE]], flags: DIFlagFwdDecl
// CHECK-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "IncompleteEnum", file: ![[FILE]], size: 32, flags: DIFlagFwdDecl

// DEFAULT-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Dynamic", file: ![[FILE:[0-9]+]], line: 11, size: 128, flags: DIFlagFwdDecl
// DEFAULT-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Incomplete", file: ![[FILE]], line: 8, flags: DIFlagFwdDecl
// DEFAULT-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "IncompleteEnum", file: ![[FILE]], line: 9, size: 32, flags: DIFlagFwdDecl

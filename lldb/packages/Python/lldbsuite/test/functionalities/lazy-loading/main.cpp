struct StructBehindPointer { int i; };
struct StructBehindRef { int i; };
struct StructMember { int i; };

StructBehindRef struct_instance;

struct SomeStruct {
  StructBehindPointer *ptr;
  StructBehindRef &ref = struct_instance;
  StructMember member;
};

struct OtherStruct {
  int member_int;
};

struct ClassBehindPointer { int i; };
struct ClassBehindRef { int i; };
struct ClassMember { int i; };

ClassBehindRef class_instance;

struct SomeClass {
  ClassBehindPointer *ptr;
  ClassBehindRef &ref = class_instance;
  ClassMember member;
};

namespace NS {
class ClassInNamespace {
};
};

int main(int argc, char **argv) {
  SomeStruct struct_var;
  OtherStruct other_struct_var;
  SomeClass some_class;
  NS::ClassInNamespace namespace_class;
  return 0; // Set break point at this line.
}

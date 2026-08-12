#include "plugin.h"

// A class template. Instantiating 'Vec<int>' produces a DWARF DIE (and,
// once parsed, a Clang 'ClassTemplateSpecializationDecl') whose fully
// qualified name is the regex-metacharacter-laden string 'Vec<int>' --
// '<' and '>' are regex metacharacters in some regex flavors and are
// always present in any template instantiation's demangled/qualified
// name. Critically, LLDB's DWARFASTParserClang builds this
// ClassTemplateSpecializationDecl by hand (marking it
// TSK_ExplicitSpecialization) without ever calling
// setTemplateArgsAsWritten() on it, unlike a specialization Clang's own
// Sema would produce while actually parsing source code.
template <typename T> struct Vec {
  T data[4];
  T Get(int i) { return data[i]; }
};

// A nested class ('Outer::Inner') whose qualified name contains '::' --
// not a regex metacharacter by itself, but combined with the operator
// overloads below this exercises a qualified name make up of many
// different kinds of "weird for a regex" punctuation.
struct Outer {
  struct Inner {
    int value = 42;
  };
};

// A class with several operator overloads. The Itanium-demangled/Clang
// qualified names of these methods are literally:
//   Op::operator()(int)
//   Op::operator[](int)
//   Op::operator+(Op const&)
// i.e. they contain '(', ')', '[', ']', '+' -- all regex metacharacters
// -- directly in the DWARF DW_AT_name / qualified name that
// "target modules dump ast --filter <name>" name-matches against.
struct Op {
  int x = 1;
  int operator()(int y) { return x + y; }
  int operator[](int i) { return x + i; }
  Op operator+(const Op &rhs) const {
    Op result;
    result.x = x + rhs.x;
    return result;
  }
};

Vec<int> g_vec_from_main;
Outer::Inner g_inner_from_main;
Op g_op_from_main;

void main_entry() {}

int main() {
  g_vec_from_main.data[0] = 1;
  g_inner_from_main.value = 43;
  g_op_from_main(3);
  g_op_from_main[2];
  g_op_from_main = g_op_from_main + g_op_from_main;

  plugin_init();
  plugin_entry();
  main_entry();
  return 0;
}

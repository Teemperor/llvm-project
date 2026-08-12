#include "plugin.h"

// A Lisp-style compile-time cons list. 'Cons<Head, Tail...>' is the
// recursive case (holds a 'Head' plus a typed pointer to the "rest" of the
// list, i.e. 'Cons<Tail...>'), and 'Cons<Head>' (an explicit specialization
// with an empty 'Tail...' pack) is the base case that terminates the
// recursion.
//
// Note: deliberately not shared via a common header. Both this file and
// plugin.cpp define the exact same recursive case ('Cons<Head, Tail...>'),
// byte-for-byte identical between the two translation units. It is only
// the *base case* ('Cons<Head>') that plugin.cpp redefines with an extra
// field (see plugin.cpp) - a genuine ODR violation, but one that is
// textually invisible at the top of the template chain and only appears
// once the recursion bottoms out.
//
// Instantiating 'Cons<int,int,int>' recursively instantiates
// 'Cons<int,int>' and, at the bottom, the base case 'Cons<int>'. Because
// the ODR conflict lives only in that base case, completing
// 'Cons<int,int,int>' forces the ASTImporter to walk down through the
// (identical) 'Cons<int,int>' before it ever reaches the conflicting
// decl - i.e. the recursion terminates *in* the conflict rather than
// starting there. Any recursion guard tailored for a self-referential
// type that conflicts with itself at the top (see
// weird-ast-recursive-template-odr) may not be set up to cover a cycle
// that only closes several template-instantiation levels down.
template <typename Head, typename... Tail> struct Cons {
  Head head;
  Cons<Tail...> *rest;
};

template <typename Head> struct Cons<Head> {
  Head head;
  void *rest;
};

// Force instantiation of Cons<int,int,int>, which recursively instantiates
// Cons<int,int> and (at the bottom) Cons<int>, entirely using main.cpp's
// (non-conflicting) definitions.
Cons<int, int, int> main_list = {1, nullptr};

int main() {
  main_list.rest = new Cons<int, int>();
  main_list.rest->head = 2;
  main_list.rest->rest = new Cons<int>();
  main_list.rest->rest->head = 3;
  main_list.rest->rest->rest = nullptr;

  plugin_init(&main_list);
  plugin_entry();
  return 0;
}

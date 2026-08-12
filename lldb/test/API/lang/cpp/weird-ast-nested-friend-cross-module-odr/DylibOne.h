#ifndef DYLIB_ONE_H_IN
#define DYLIB_ONE_H_IN

// This dylib's 'Outer' has a nested 'class Inner' that befriends its
// enclosing class (a FriendDecl attached to Inner's DeclContext, granting
// Outer access to Inner's private 'secret' field). See DylibTwo.h for the
// conflicting definition: same qualified name 'Outer::Inner', but a
// 'struct' instead of a 'class', no friend declaration, and an extra
// field.
class Outer {
public:
  class Inner {
    friend class Outer;
    int secret;

  public:
    Inner(int s) : secret(s) {}
  };

  Inner make() { return Inner(1); }
};

extern "C" {
void dylib_one_init(void);
}

extern Outer *a_outer;

#endif // DYLIB_ONE_H_IN

struct SingleMember {
  int i;
};

struct Outer {
  struct SingleMember m;
};

int main() {
  struct Outer outer;
  outer.m.i = 4;

  return 0; // break here
}

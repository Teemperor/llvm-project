
struct SingleMember {
  int i;
};

struct BaseClass {
  long x;
};

struct Outer : BaseClass{
  struct SingleMember m;
};

int main() {
  struct Outer outer;
  outer.m.i = 4;
  outer.x = -22;

  return 0; // break here
}

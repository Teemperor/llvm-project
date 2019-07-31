class D {};
class E {
  D f();
};
struct S {
  S(E){}
};
int main() { E x; S y(x); }

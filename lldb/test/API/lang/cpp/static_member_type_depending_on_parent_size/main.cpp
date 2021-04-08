template <typename T> struct S1 : T {};
template <typename T> struct S2 { S1<T> m; };
struct Cs {
  static S2<Cs> d;
  int cm;
};

S2<Cs> Cs::d;
Cs r;

int main() {
  return r.cm; // stophere and run 'target var r'
}

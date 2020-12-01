template <class T, int... Args> struct C {
  T member;
  bool argsAre_16_32() { return false; }
};

template <> struct C<int, 16> {
  int member;
  bool argsAre_16_32() { return false; }
};

template <> struct C<int, 16, 32> : C<int, 16> {
  bool argsAre_16_32() { return true; }
};

template <class T, typename... Args> struct D {
  T member;
  bool argsAre_Int_bool() { return false; }
};

template <> struct D<int, int> {
  int member;
  bool argsAre_Int_bool() { return false; }
};

template <> struct D<int, int, bool> : D<int, int> {
  bool argsAre_Int_bool() { return true; }
};

// Unnamed type parameter.
template <typename... >
struct AnonTypePack { int m; };
AnonTypePack<int> emptyAnonTypePackVar;
AnonTypePack<int, float> oneElemAnonTypePackVar;

// Unnamed type parameter pack behind a named type parameter.
template <typename T, typename... >
struct AnonTypePackAfterParam { T n; };
AnonTypePackAfterParam<int> emptyAnonTypePackAfterParamVar;
AnonTypePackAfterParam<int, float> oneElemAnonTypePackAfterParamVar;

// Unnamed type parameter pack behind an unnamed type parameter.
template <typename, typename... >
struct AnonTypePackAfterAnonParam { float j; };
AnonTypePackAfterAnonParam<int> emptyAnonTypePackAfterAnonParamVar;
AnonTypePackAfterAnonParam<int, float> oneElemAnonTypePackAfterAnonParamVar;

// Unnamed type parameter pack behind an unnamed type parameter.
template <typename, typename... Ts>
struct TypePackAfterAnonParam { int k; };
TypePackAfterAnonParam<int> emptyTypePackAfterAnonParamVar;
TypePackAfterAnonParam<int, float> oneElemTypePackAfterAnonParamVar;

int main(int argc, char const *argv[]) {
  C<int, 16, 32> myC;
  C<int, 16> myLesserC;
  myC.member = 64;
  (void)C<int, 16, 32>().argsAre_16_32();
  (void)C<int, 16>().argsAre_16_32();
  (void)(myC.member != 64);
  D<int, int, bool> myD;
  D<int, int> myLesserD;
  myD.member = 64;
  (void)D<int, int, bool>().argsAre_Int_bool();
  (void)D<int, int>().argsAre_Int_bool();

  return 0; // break here
}

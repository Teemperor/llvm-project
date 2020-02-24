#include <cstdlib>

int side_effect = 0;

struct B { int dummy = 2324; };
struct C {
  void *operator new(std::size_t size) { C* r = ::new C; r->custom_new = true; return r; }
  void *operator new[](std::size_t size) { C* r = static_cast<C*>(std::malloc(size)); r->custom_new = true; return r; }
  void operator delete(void *p) { std::free(p); side_effect = 1; }
  void operator delete[](void *p) { std::free(p); side_effect = 2; }

  bool custom_new = false;
  B b;
  B* operator->() { return &b; }
  int operator->*(int) { return 2; }
  int operator+(int) { return 44; }
  int operator+=(int) { return 42; }
  int operator++(int) { return 123; }
  int operator++() { return 1234; }
  int operator-(int) { return 34; }
  int operator-=(int) { return 32; }
  int operator--() { return 321; }
  int operator--(int) { return 4321; }

  int operator*(int) { return 51; }
  int operator*=(int) { return 52; }
  int operator%(int) { return 53; }
  int operator%=(int) { return 54; }
  int operator/(int) { return 55; }
  int operator/=(int) { return 56; }
  int operator^(int) { return 57; }
  int operator^=(int) { return 58; }

  int operator|(int) { return 61; }
  int operator|=(int) { return 62; }
  int operator||(int) { return 63; }
  int operator&(int) { return 64; }
  int operator&=(int) { return 65; }
  int operator&&(int) { return 66; }

  int operator~() { return 71; }
  int operator!() { return 72; }
  int operator!=(int) { return 73; }
  int operator=(int) { return 74; }
  int operator==(int) { return 75; }

  int operator<(int) { return 81; }
  int operator<<(int) { return 82; }
  int operator<=(int) { return 83; }
  int operator<<=(int) { return 84; }
  int operator>(int) { return 85; }
  int operator>>(int) { return 86; }
  int operator>=(int) { return 87; }
  int operator>>=(int) { return 88; }

  int operator,(int) { return 2012; }
  int operator&() { return 2013; }

  int operator()(int) { return 91; }
  int operator[](int) { return 92; }

  operator int() { return 11; }
  operator long() { return 12; }

  // Make sure this doesn't collide with
  // the real operator int.
  int operatorint() { return 13; }
  int operatornew() { return 14; }
};

int main(int argc, char **argv) {
  C c;
  int result = c->dummy;
  result = c->*4;
  result += c+1;
  result += c+=1;
  result += c++;
  result += ++c;
  result += c-1;
  result += c-=1;
  result += c--;
  result += --c;

  result += c * 4;
  result += c *= 4;
  result += c % 4;
  result += c %= 4;
  result += c / 4;
  result += c /= 4;
  result += c ^ 4;
  result += c ^= 4;

  result += c | 4;
  result += c |= 4;
  result += c || 4;
  result += c & 4;
  result += c &= 4;
  result += c && 4;

  result += ~c;
  result += !c;
  result += c!=1;
  result += c=2;
  result += c==2;

  result += c<2;
  result += c<<2;
  result += c<=2;
  result += c<<=2;
  result += c>2;
  result += c>>2;
  result += c>=2;
  result += c>>=2;

  result += (c , 2);
  result += &c;

  result += c(1);
  result += c[1];

  result += static_cast<int>(c);
  result += static_cast<long>(c);
  result += c.operatorint();
  result += c.operatornew();

  C *c2 = new C();
  C *c3 = new C[3];

  //%# self.expect("log enable lldb expr")
  //% self.expect("expr (new C)->custom_new", endstr=" true\n")
  //% self.expect("expr (new C[1])->custom_new", endstr=" true\n")
  delete c2;
  delete[] c3;
  return 0;
}

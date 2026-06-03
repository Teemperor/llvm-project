struct IntStruct {
  int a;
  int b;
};

union IntUnion {
  int as_int;
  char as_char;
};

class IntClass {
public:
  IntClass(int a, int b) : m_a(a), m_b(b) {}

  int m_a;
  int m_b;
};

int main() {
  // Scalar variables of every integer type. The char-like types use printable
  // characters so their values are easy to check.
  char the_char = 'a';
  signed char the_signed_char = 'B';
  unsigned char the_unsigned_char = 'Z';
  short the_short = -31987;
  unsigned short the_unsigned_short = 65000;
  int the_int = -1100110;
  unsigned int the_unsigned_int = 4000000000u;
  long the_long = -1100110100l;
  unsigned long the_unsigned_long = 1100110100ul;
  long long the_long_long = -110011001100ll;
  unsigned long long the_unsigned_long_long = 110011001100ull;

  // Different "shapes" of the int type to check that pointers, references,
  // arrays, structs, unions and classes of integers are displayed correctly.
  int the_int_array[3] = {1, 2, 3};
  int *the_int_ptr = &the_int;
  int &the_int_ref = the_int;
  IntStruct the_int_struct = {10, 20};
  IntUnion the_int_union;
  the_int_union.as_int = 42;
  IntClass the_int_class(50, 60);
  IntClass *the_int_class_ptr = &the_int_class;
  IntClass &the_int_class_ref = the_int_class;

  // Edge-case values: smallest, largest, zero and -1. The char-like types are
  // displayed as character literals, so their values use escape sequences.
  char char_zero = 0;
  signed char schar_neg_one = -1;
  signed char schar_min = -128;
  signed char schar_max = 127;
  unsigned char uchar_zero = 0;
  unsigned char uchar_max = 255;

  short short_min = -32768;
  short short_max = 32767;
  short short_zero = 0;
  short short_neg_one = -1;
  unsigned short ushort_zero = 0;
  unsigned short ushort_max = 65535;

  int int_min = -2147483647 - 1;
  int int_max = 2147483647;
  int int_zero = 0;
  int int_neg_one = -1;
  unsigned int uint_zero = 0;
  unsigned int uint_max = 4294967295u;

  long long llong_min = -9223372036854775807ll - 1;
  long long llong_max = 9223372036854775807ll;
  long long llong_zero = 0;
  long long llong_neg_one = -1;
  unsigned long long ullong_zero = 0;
  unsigned long long ullong_max = 18446744073709551615ull;

  return 0; // break here
}

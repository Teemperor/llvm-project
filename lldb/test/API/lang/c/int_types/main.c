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

  // Edge-cases.
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

  // The width of 'long' depends on the data model (64 bits under LP64, 32 bits
  // under LLP64/ILP32), so its min/max are computed portably instead of being
  // spelled out. Zero and -1 have the same representation regardless of width.
  long long_zero = 0;
  long long_neg_one = -1;
  unsigned long ulong_zero = 0;
  long long_max = (long)(~0ul >> 1);
  long long_min = -long_max - 1;
  unsigned long ulong_max = ~0ul;

  long long llong_min = -9223372036854775807ll - 1;
  long long llong_max = 9223372036854775807ll;
  long long llong_zero = 0;
  long long llong_neg_one = -1;
  unsigned long long ullong_zero = 0;
  unsigned long long ullong_max = 18446744073709551615ull;

  return 0; // break here
}

struct FloatStruct {
  float a;
  double b;
};

union FloatUnion {
  double as_double;
  float as_float;
};

class FloatClass {
public:
  FloatClass(float a, double b) : m_a(a), m_b(b) {}

  float m_a;
  double m_b;
};

int main() {
  // Scalar variables of every floating point type. The values are all exactly
  // representable so their printed value is unambiguous.
  float the_float = 3.5f;
  double the_double = 6.25;
  long double the_long_double = 10.75;

  // Different "shapes" of the double type to check that pointers, references,
  // arrays, structs, unions and classes of floats are displayed correctly.
  double the_double_array[3] = {1.5, 2.5, 3.5};
  double *the_double_ptr = &the_double;
  double &the_double_ref = the_double;
  FloatStruct the_float_struct = {0.5f, 0.25};
  FloatUnion the_float_union;
  the_float_union.as_double = 0.125;
  FloatClass the_float_class(7.5f, 8.25);
  FloatClass *the_float_class_ptr = &the_float_class;
  FloatClass &the_float_class_ref = the_float_class;

  // Edge-case values: zero, -1 and a negative value.
  float float_zero = 0.0f;
  float float_neg_one = -1.0f;
  float float_neg = -2.5f;
  double double_zero = 0.0;
  double double_neg_one = -1.0;
  double double_neg = -2.5;
  long double long_double_zero = 0.0;
  long double long_double_neg_one = -1.0;

  return 0; // break here
}

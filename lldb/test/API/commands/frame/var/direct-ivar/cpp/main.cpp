struct Structure {
  int m_field;
  void fun() {
    // break here
  }
};

int main() {
  Structure s;
  s.m_field = 30;
  s.fun();
}

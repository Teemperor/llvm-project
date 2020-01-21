struct Class {
  Class &get() { return *this; }
};

int main() {
  Class c;
  c.get(); //% self.expect_expr("Class f;")
}

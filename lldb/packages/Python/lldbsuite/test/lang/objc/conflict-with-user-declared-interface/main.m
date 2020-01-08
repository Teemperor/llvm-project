@class SomeClass;

int main() {
  SomeClass *c;
  return 0; //%self.expect("expr c", substrs=["SomeClass"]) # Get SomeClass in the scratch context.
            //%self.expect("expr --top-level -- @class SomeClass;") # This shouldn't crash.
}

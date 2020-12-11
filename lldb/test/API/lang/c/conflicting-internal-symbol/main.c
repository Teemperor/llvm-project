static int global_var2 = 1;

int UseOtherGlobal();

int main(int argc, char **argv) {
  return global_var2 + UseOtherGlobal();; // break here
}

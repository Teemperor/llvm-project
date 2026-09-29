void pause() {}

int foo(int a, int b) {
  int vla[a];
  int vla_2d[a][b];
  int vla_mixed[a][3];

  for (int i = 0; i < a; ++i) {
    vla[i] = i;
    for (int j = 0; j < b; ++j)
      vla_2d[i][j] = i + j;
    for (int j = 0; j < 3; ++j)
      vla_mixed[i][j] = i * j;
  }

  pause(); // break here
  return vla[a - 1] + vla_2d[a - 1][b - 1] + vla_mixed[a - 1][2];
}

int main(void) { return foo(2, 3) + foo(4, 5); }

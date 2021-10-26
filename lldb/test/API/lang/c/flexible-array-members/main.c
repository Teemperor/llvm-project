#include <stdlib.h>
#include <string.h>

struct WithFlex {
  int member;
  char flexible[];
};

#define CONTENTS "contents"

int main() {
  struct WithFlex *s = (struct WithFlex*)malloc(sizeof(int) + sizeof(CONTENTS));
  s->member = 1;
  strcpy(s->flexible, CONTENTS);
  return 0; // break here
}

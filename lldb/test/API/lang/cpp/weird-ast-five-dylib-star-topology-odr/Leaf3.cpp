// Leaf3 defines 'struct Payload' with a 'char *' data member.
struct Payload {
  int kind;
  char *data;
};

char gStr3[] = "three";
Payload gPayload3 = {3, gStr3};

extern "C" Payload *MakePayload3() { return &gPayload3; }

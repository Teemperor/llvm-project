// Leaf1 defines 'struct Payload' with an 'int' data member.
struct Payload {
  int kind;
  int data;
};

Payload gPayload1 = {1, 111};

extern "C" Payload *MakePayload1() { return &gPayload1; }

// Leaf2 defines 'struct Payload' with a 'double' data member.
struct Payload {
  int kind;
  double data;
};

Payload gPayload2 = {2, 2.5};

extern "C" Payload *MakePayload2() { return &gPayload2; }

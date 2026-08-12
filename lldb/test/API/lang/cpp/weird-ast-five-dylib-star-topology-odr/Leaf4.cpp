// Leaf4 defines 'struct Payload' with a 'bool' data member.
struct Payload {
  int kind;
  bool data;
};

Payload gPayload4 = {4, true};

extern "C" Payload *MakePayload4() { return &gPayload4; }

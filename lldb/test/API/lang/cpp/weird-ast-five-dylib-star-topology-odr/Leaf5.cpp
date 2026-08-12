// Leaf5 defines 'struct Payload' with an anonymous-struct data member
// (yet another, differently shaped, incompatible definition of the same
// tag name 'Payload').
struct Payload {
  int kind;
  struct {
    short s;
  } data;
};

Payload gPayload5 = {5, {55}};

extern "C" Payload *MakePayload5() { return &gPayload5; }

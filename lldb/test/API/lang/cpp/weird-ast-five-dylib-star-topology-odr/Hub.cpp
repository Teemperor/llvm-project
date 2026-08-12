// Hub aggregates one opaque 'Payload *' from each of the five leaf
// dylibs into a single 'AllPayloads' struct. Hub itself never sees any
// leaf's definition of 'struct Payload': it only forward declares the
// tag so it can hold five typed-but-opaque pointers, one per leaf.
struct Payload;

struct AllPayloads {
  Payload *p[5];
};

extern "C" {
Payload *MakePayload1();
Payload *MakePayload2();
Payload *MakePayload3();
Payload *MakePayload4();
Payload *MakePayload5();
}

AllPayloads gAllPayloads;

extern "C" AllPayloads *MakeAllPayloads() {
  gAllPayloads.p[0] = MakePayload1();
  gAllPayloads.p[1] = MakePayload2();
  gAllPayloads.p[2] = MakePayload3();
  gAllPayloads.p[3] = MakePayload4();
  gAllPayloads.p[4] = MakePayload5();
  return &gAllPayloads;
}

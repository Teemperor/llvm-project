namespace {
struct alignas(8) ClassWithSize { char c; };
} //namespace

ClassWithSize class_with_size_8;

int other() { return class_with_size_8.c; }

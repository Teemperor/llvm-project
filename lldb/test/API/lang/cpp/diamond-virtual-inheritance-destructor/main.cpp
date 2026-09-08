// A diamond of virtual inheritance: two sibling classes both virtually
// inherit the same abstract base, and the most-derived class must
// re-override the base's pure virtual to disambiguate the siblings'
// conflicting final overriders (mandatory: without it, this is a compile
// error). None of the four classes declares its own destructor, so each of
// ~RecencyTracker/~FrequencyTracker/~ArcPolicy is implicit, but still
// virtual (it overrides Trackable's virtual destructor).
struct Trackable {
  virtual ~Trackable() = default;
  virtual int weight() const = 0;
};

struct RecencyTracker : public virtual Trackable {
  int weight() const override { return 1; }
};

struct FrequencyTracker : public virtual Trackable {
  int weight() const override { return 2; }
};

struct ArcPolicy : public RecencyTracker, public FrequencyTracker {
  int weight() const override { return 3; }
};

ArcPolicy g_policy;

void stop() {}

int main() {
  stop(); // break here
  return g_policy.weight();
}

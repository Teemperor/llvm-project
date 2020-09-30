#include <set>

struct GlobalOp {
  int x;
};

bool operator<(const GlobalOp &lhs, const GlobalOp &rhs) {
  return lhs.x > rhs.x;
}

struct Op {
  int x;
  bool operator<(const Op &o) const {
    return x > o.x;
  }
};

struct Functor {
  int x;
};

struct FunctorCmp {
  bool operator() (const Functor& lhs, const Functor& rhs) const {
    return lhs.x > rhs.x;
  }
};

int main(int argc, char **argv) {
  std::set<int> int_set = {1, 2, 3, 4, 5};
  std::set<GlobalOp> global_op_set = {
    {3},
    {2},
    {1}
  };
  std::set<Op> op_set = {
    {3},
    {2},
    {1}
  };
  std::set<Functor, FunctorCmp> functor_set = {
    {3},
    {2},
    {1}
  };
  return 0; // Set break point at this line.
}

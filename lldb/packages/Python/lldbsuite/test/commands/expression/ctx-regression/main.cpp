#include <map>

int main(int argc, char const* argv[]) {
    std::map<int, int> l;

    l[22] = 333;
    return 0; //%self.expect("expr l", substrs=['333'])
}

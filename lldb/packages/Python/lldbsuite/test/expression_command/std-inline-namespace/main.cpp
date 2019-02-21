#include <string>

int main(int argc, char **argv) {
    std::string f = "abc";
    // lldb testsuite break
    return f.size() + std::string::npos;
}

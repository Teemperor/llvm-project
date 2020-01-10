#include <stdio.h>
#include <string>
#include <vector>
typedef std::vector<int> int_vect;
typedef std::vector<std::string> string_vect;

int main()
{
    string_vect strings;
    (strings.push_back(std::string("goofy")));
    (strings.push_back(std::string("is")));
    (strings.push_back(std::string("smart")));
    return 0;  // break here
}

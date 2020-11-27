#include <string>

int main(int argc, char **argv) {
  std::string s = "abc";
  std::wstring ws = L"abc";
  std::u16string s16 = u"abc";
  std::u32string s32 = U"abc";
  std::basic_string<char> bs = "abc";
  return 0; // Set break point at this line.
}

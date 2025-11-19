#include <cstdlib>
#include <iostream>

int main() {
  int result = std::system("bunzip2 -f ../data/20250301.as-rel2.txt.bz2");
  if (result != 0) {
    std::cerr << "Failed to decompress" << std::endl;
    return 1;
  }
  std::cout << "Working" << std::endl;
  return 0;
}

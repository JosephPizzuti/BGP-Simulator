#include <fstream>
#include <stdexcept>
#include "parser.hpp"

inline int fast_atoi(std::string_view sv) noexcept {
  const char* p = sv.data();
  const char* end = p + sv.size();
  
  if (p != end && *p == '-') {
    return -1;
  }

  int value = 0;
  while (p != end) {
    value = value * 10 + (*p - '0');
    ++p;
  }
  return value;
}

bool parse_line(std::string_view line, DataRecord& rec) {
  size_t pos1 = line.find('|');
  size_t pos2 = line.find('|', pos1 + 1);
  size_t pos3 = line.find('|', pos2 + 1);
  if (pos1 == std::string_view::npos) return false;
  if (pos2 == std::string_view::npos) return false;
  if (pos3 == std::string_view::npos) return false;

  std::string_view s1 = line.substr(0, pos1);
  std::string_view s2 = line.substr(pos1 + 1, pos2 - pos1 - 1);
  std::string_view s3 = line.substr(pos2 + 1, pos3 - pos2 - 1);
  
  rec.provider_peer = fast_atoi(s1);
  rec.customer_peer = fast_atoi(s2);
  rec.indicator     = fast_atoi(s3);

  return true;
}


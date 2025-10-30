#pragma once
#include <fstream>
#include <string>
#include <stdexcept>
#include "parser.hpp"
#include "data_record.hpp"

template <typename Fn>
void read_caida_data(const std::string& filename, Fn&& handle_record) {
  std::ifstream data(filename);
  if (!data.is_open()) {
    throw std::runtime_error("Failed to open file: " + filename);
  }

  std::string line;
  line.reserve(128);
  DataRecord rec;

  // skips past all headers and empty lines
  while (std::getline(data, line)) {
    if (!line.empty() && line[0] != '#') {
      break;
    }
  }
  if (!data) return;
  
  // reading data
  do {
    if (!parse_line(line, rec)) {
      throw std::runtime_error("Malformed line found: " + line);
    }
    handle_record(rec);
  } while (std::getline(data, line));
}

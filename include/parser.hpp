#pragma once
#include <string_view>
#include "data_record.hpp"

bool parse_line(std::string_view line, DataRecord& rec);
int fast_atoi(std::string_view sv) noexcept;

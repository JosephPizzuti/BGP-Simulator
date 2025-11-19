#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "announcement.hpp"

class Policy
{
public:
  virtual ~Policy() = default;
  virtual uint32_t asn() const noexcept = 0;
  virtual void enqueue(const Announcement& ann) = 0;
  virtual bool has_pending() const = 0;
  virtual void process_pending() = 0;
  virtual const std::unordered_map<std::string, Announcement>& local_rib() const = 0;

};

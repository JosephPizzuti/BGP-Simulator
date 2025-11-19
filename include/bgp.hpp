#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include "policy.hpp"
#include "announcement.hpp"

class BGPPolicy : public Policy
{
  uint32_t asn_;
  std::unordered_map<std::string, Announcement> local_rib_;
  std::unordered_map<std::string, std::vector<Announcement>> received_;

public:
  explicit BGPPolicy(uint32_t asn) : asn_(asn) {}
  uint32_t asn() const noexcept override
  {
    return asn_;
  }
  void enqueue(const Announcement& ann) override
  {
    received_[ann.prefix].push_back(ann);
  }
  bool has_pending() const override
  {
    for (const auto& kv : received_)
    {
      if (!kv.second.empty()) return true;
    }
    return false;
  }
  void process_pending() override
  {
    for (auto& kv : received_)
    {
      const std::string& prefix = kv.first;
      std::vector<Announcement>& candidates = kv.second;
      if (candidates.empty()) continue;

      Announcement best = candidates[0];
      for (std::size_t i = 1; i < candidates.size(); ++i)
      {
        if (better_announcement(candidates[i], best))
          best = candidates[i];
      }

      auto it = local_rib_.find(prefix);
      if (it == local_rib_.end())
        local_rib_[prefix] = std::move(best);
      else
        if (better_announcement(best, it->second))
          it->second = std::move(best);
    }

    received_.clear();
  }
  const std::unordered_map<std::string, Announcement>& local_rib() const override
  {
    return local_rib_;
  }

};

#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>

#include "as_graph.hpp"
#include "bgp.hpp"
#include "announcement.hpp"

class BGPSim
{
  static Announcement make_forwarded(const Announcement& base,
                                     uint32_t from_asn,
                                     uint32_t to_asn,
                                     Relationship rel_at_receiver)
  {
    Announcement out;
    out.prefix = base.prefix;

    out.as_path.reserve(base.as_path.size() + 1);
    out.as_path.push_back(to_asn);
    out.as_path.insert(out.as_path.end(),
                       base.as_path.begin(),
                       base.as_path.end());

    out.next_hop_asn = from_asn;
    out.received_from = rel_at_receiver;

    return out;
  }

  const ASGraph& graph_;
  std::vector<BGPPolicy> policies_;
  std::vector<std::vector<uint32_t>> layers_;

public:
  explicit BGPSim(const ASGraph& graph)
    : graph_(graph),
      layers_(flatten_graph(graph))
  {
    const std::size_t n = graph_.size();
    policies_.reserve(n);

    policies_.emplace_back(0);

    for (uint32_t asn = 1; asn < n; ++asn)
      policies_.emplace_back(asn);
  }

  uint32_t max_asn() const noexcept
  {
    return static_cast<uint32_t>(policies_.size() - 1);
  }

  BGPPolicy& policy(uint32_t asn)
  {
    return policies_.at(asn);
  }

  const BGPPolicy& policy(uint32_t asn) const
  {
    return policies_.at(asn);
  }

  const std::vector<std::vector<uint32_t>>& layers() const noexcept
  {
    return layers_;
  }

  void seed_prefix(const std::string& prefix, uint32_t origin_asn)
  {
    if (origin_asn == 0 || origin_asn >= graph_.size())
      throw std::runtime_error("seed_prefix: origin ASN out of range");

    Announcement a = make_origin_announcement(prefix, origin_asn);

    auto& pol = policies_.at(origin_asn);
    pol.enqueue(a);
    pol.process_pending();
  }

  void propagate_up()
  {
    const std::size_t num_ranks = layers_.size();
    if (num_ranks == 0) return;

    for (std::size_t r = 0; r < num_ranks; ++r)
    {
      for (uint32_t asn : layers_[r])
      {
        const ASNode& node = graph_.get(asn);
        const auto& rib = policies_[asn].local_rib();
        if (rib.empty()) continue;

        for (const auto& kv : rib)
        {
          const Announcement& ann = kv.second;
          for (uint32_t provider : node.providers)
          {
            Announcement out = make_forwarded
            (
              ann,
              asn,
              provider,
              Relationship::FROM_CUSTOMER
            );
            policies_[provider].enqueue(out);
          }
        }
      }

      if (r + 1 < num_ranks)
      {
        for (uint32_t asn : layers_[r + 1])
        {
          if (policies_[asn].has_pending())
            policies_[asn].process_pending();
        }
      }
    }
  }

  void propagate_across_peers()
  {
    const std::size_t n = graph_.size();

    for (uint32_t asn = 1; asn < n; ++asn)
    {
      const ASNode& node = graph_.get(asn);
      const auto& rib = policies_[asn].local_rib();
      if (rib.empty()) continue;

      for (const auto& kv : rib)
      {
        const Announcement& ann = kv.second;
        for (uint32_t peer : node.peers)
        {
          Announcement out = make_forwarded
          (
            ann,
            asn,
            peer,
            Relationship::FROM_PEER
          );
          policies_[peer].enqueue(out);
        }
      }
    }

    for (uint32_t asn = 1; asn < n; ++asn)
    {
      if (policies_[asn].has_pending())
        policies_[asn].process_pending();
    }
  }

  void propagate_down()
  {
    if (layers_.empty()) return;
    const std::size_t num_ranks = layers_.size();

    for (std::size_t r = num_ranks - 1; r > 0; --r)
    {
      for (uint32_t asn : layers_[r])
      {
        const ASNode& node = graph_.get(asn);
        const auto& rib = policies_[asn].local_rib();
        if (rib.empty()) continue;

        for (const auto& kv : rib)
        {
          const Announcement& ann = kv.second;
          for (uint32_t customer : node.customers)
          {
            Announcement out = make_forwarded
            (
              ann,
              asn,
              customer,
              Relationship::FROM_PROVIDER
            );
            policies_[customer].enqueue(out);
          }
        }
      }

      const std::size_t lower_rank = r - 1;
      for (uint32_t asn : layers_[lower_rank])
      {
        if (policies_[asn].has_pending())
          policies_[asn].process_pending();
      }
    }
  }

  void propagate_all()
  {
    propagate_up();
    propagate_across_peers();
    propagate_down();
  }
};
